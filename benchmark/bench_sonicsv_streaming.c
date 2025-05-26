#define SONICSV_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>

#include "../include/sonicsv.h"

// Global configuration
typedef struct {
    bool verbose;
    csv_parse_options_t options;
    size_t buffer_size;
    uint64_t rows;
    uint64_t fields;
} Config;

static Config g_config;
static volatile sig_atomic_t csv_abort_requested = 0;

// Get current time in seconds with high precision
static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// Row callback for SonicSV streaming
static void row_callback(const csv_row_t* row, void* user_data) {
    Config* config = (Config*)user_data;
    config->rows++;
    config->fields += row->num_fields;
    
    if (csv_abort_requested) return;
    
    if (config->verbose && (config->rows % 100000 == 0)) {
        printf("\r-i-- Processing rows: %lu", config->rows);
        fflush(stdout);
    }
}

// Error callback for SonicSV
static void error_callback(csv_error_t error, const char* message, uint64_t row_number, void* user_data) {
    if (g_config.verbose) {
        fprintf(stderr, "\n-e-- CSV Error at row %lu: %s (%s)\n", 
                row_number, message, csv_error_string(error));
    }
}

// Progress callback for SonicSV
static void progress_callback(uint64_t bytes_processed, uint64_t total_bytes, void* user_data) {
    if (g_config.verbose && total_bytes > 0) {
        double progress = (double)bytes_processed / total_bytes * 100.0;
        printf("\r-i-- Progress: %.1f%% (%lu/%lu bytes)", progress, bytes_processed, total_bytes);
        fflush(stdout);
    }
}

// Signal handler
static void csv_signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM)
        csv_abort_requested = 1;
}

// Usage information
void print_usage(const char* program_name) {
    fprintf(stderr, "Usage: %s [options] <csvfile>\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -d, --delimiter=C   Set delimiter character (default: ',')\n");
    fprintf(stderr, "  -q, --quote=C       Set quote character (default: '\"')\n");
    fprintf(stderr, "  -e, --escape=C      Set escape character (default: '\\')\n");
    fprintf(stderr, "  -b, --buffer=SIZE   Set buffer size in KB (default: 256)\n");
    fprintf(stderr, "  --no-quoting        Disable quote handling\n");
    fprintf(stderr, "  --no-escaping       Disable escape handling\n");
    fprintf(stderr, "  --trim-whitespace   Trim whitespace from fields\n");
    fprintf(stderr, "  --ignore-empty      Ignore empty lines\n");
    fprintf(stderr, "  -v, --verbose       Show detailed progress\n");
    fprintf(stderr, "  -h, --help          Show this help message\n");
    fprintf(stderr, "\nSonicSV Streaming Mode with SIMD Features:\n");
    
    uint32_t features = csv_detect_simd_features();
    fprintf(stderr, "  Available: ");
    if (features & CSV_SIMD_SSE4_2) fprintf(stderr, "SSE4.2 ");
    if (features & CSV_SIMD_AVX2) fprintf(stderr, "AVX2 ");
    if (features & CSV_SIMD_NEON) fprintf(stderr, "NEON ");
    if (features == CSV_SIMD_NONE) fprintf(stderr, "None");
    fprintf(stderr, "\n");
}

int main(int argc, char** argv) {
    // Initialize SIMD features
    csv_simd_init();
    
    // Default configuration
    g_config.verbose = false;
    g_config.options = csv_default_options();
    g_config.buffer_size = 256 * 1024;
    g_config.rows = 0;
    g_config.fields = 0;
    
    // Command line options
    static struct option long_options[] = {
        {"delimiter",       required_argument, 0, 'd'},
        {"quote",           required_argument, 0, 'q'},
        {"escape",          required_argument, 0, 'e'},
        {"buffer",          required_argument, 0, 'b'},
        {"no-quoting",      no_argument,       0, 1000},
        {"no-escaping",     no_argument,       0, 1001},
        {"trim-whitespace", no_argument,       0, 1002},
        {"ignore-empty",    no_argument,       0, 1003},
        {"verbose",         no_argument,       0, 'v'},
        {"help",            no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "d:q:e:b:vh", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                g_config.options.delimiter = optarg[0];
                break;
            case 'q':
                g_config.options.quote_char = optarg[0];
                break;
            case 'e':
                g_config.options.escape_char = optarg[0];
                break;
            case 'b':
                g_config.buffer_size = atoi(optarg) * 1024;
                if (g_config.buffer_size < 4096) {
                    fprintf(stderr, "-w-- Buffer size too small, using 4KB\n");
                    g_config.buffer_size = 4096;
                }
                break;
            case 1000:
                g_config.options.quoting = false;
                break;
            case 1001:
                g_config.options.escaping = false;
                break;
            case 1002:
                g_config.options.trim_whitespace = true;
                break;
            case 1003:
                g_config.options.ignore_empty_lines = true;
                break;
            case 'v':
                g_config.verbose = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Check for input file
    if (optind >= argc) {
        fprintf(stderr, "-e-- Missing input CSV file\n");
        print_usage(argv[0]);
        return 1;
    }
    
    const char* filename = argv[optind];
    
    // Get file size
    struct stat st;
    if (stat(filename, &st) != 0) {
        fprintf(stderr, "-e-- Cannot access file %s\n", filename);
        return 1;
    }
    
    size_t file_size = (size_t)st.st_size;
    if (file_size == 0) {
        fprintf(stderr, "-e-- File is empty: %s\n", filename);
        return 1;
    }
    
    // Display parsing info
    printf("-i-- Parsing mode: SonicSV Streaming (single-threaded)\n");
    printf("-i-- File: %s (%.2f MB)\n", filename, file_size / (1024.0 * 1024.0));
    printf("-i-- Options: delimiter='%c', quote='%c', escape='%c', buffer=%.2f KB\n", 
           g_config.options.delimiter, g_config.options.quote_char, 
           g_config.options.escape_char, g_config.buffer_size / 1024.0);
    
    uint32_t simd_features = csv_simd_get_features();
    printf("-i-- SIMD acceleration: ");
    if (simd_features & CSV_SIMD_SSE4_2) printf("SSE4.2 ");
    if (simd_features & CSV_SIMD_AVX2) printf("AVX2 ");
    if (simd_features & CSV_SIMD_NEON) printf("NEON ");
    if (simd_features == CSV_SIMD_NONE) printf("None");
    printf("\n");
    
    // Set up signal handling
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = csv_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // Create CSV parser
    csv_parser_t* parser = csv_parser_create(&g_config.options);
    if (!parser) {
        fprintf(stderr, "-e-- Failed to create CSV parser\n");
        return 1;
    }
    
    // Set callbacks
    csv_parser_set_row_callback(parser, row_callback, &g_config);
    csv_parser_set_error_callback(parser, error_callback, &g_config);
    if (g_config.verbose) {
        csv_parser_set_progress_callback(parser, progress_callback, &g_config);
    }
    
    // Start timing
    printf("-i-- Starting CSV parsing...\n");
    double t0 = now_sec();
    
    // Parse the file
    csv_error_t parse_error = csv_parse_file(parser, filename);
    
    double total_time = now_sec() - t0;
    
    if (g_config.verbose) {
        printf("\n");
    }
    
    // Print results
    if (parse_error == CSV_OK && !csv_abort_requested) {
        double throughput = file_size / (1024.0 * 1024.0) / total_time;
        
        printf("-s-- Parsing completed successfully\n");
        printf("-i-- Parsed %lu lines (%lu fields) in %.6f seconds (%.2e lines/sec)\n", 
               g_config.rows, g_config.fields, total_time, g_config.rows / total_time);
        printf("-i-- Throughput: %.2f MB/s\n", throughput);
        printf("-i-- Average fields per line: %.1f\n", 
               g_config.rows > 0 ? (double)g_config.fields / g_config.rows : 0.0);
        
        // Print detailed stats
        csv_stats_t stats = csv_parser_get_stats(parser);
        printf("-i-- Parser stats: %.2f MB/s, SIMD features used: 0x%x\n", 
               stats.throughput_mbps, stats.simd_acceleration_used);
        
        if (g_config.verbose) {
            csv_print_stats(parser);
        }
    } else if (csv_abort_requested) {
        fprintf(stderr, "-w-- Parsing interrupted by user\n");
    } else {
        fprintf(stderr, "-e-- Parsing failed: %s\n", csv_error_string(parse_error));
    }
    
    // Cleanup
    csv_parser_destroy(parser);
    
    return (parse_error == CSV_OK && !csv_abort_requested) ? 0 : 1;
} 