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
    csv_block_config_t config;
    uint64_t rows;
    uint64_t fields;
    uint64_t batches;
} Config;

static Config g_config;
static volatile sig_atomic_t csv_abort_requested = 0;

// Get current time in seconds with high precision
static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// Batch callback for SonicSV batching
static void batch_callback(const csv_data_batch_t* batch, void* user_data) {
    Config* config = (Config*)user_data;
    config->batches++;
    config->rows += batch->num_rows;
    config->fields += batch->num_fields_total;
    
    if (csv_abort_requested) return;
    
    if (config->verbose && (config->batches % 100 == 0)) {
        printf("\r-i-- Processing batches: %lu (rows: %lu)", config->batches, config->rows);
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
    fprintf(stderr, "  -t, --threads=N     Number of threads (default: CPU cores)\n");
    fprintf(stderr, "  -d, --delimiter=C   Set delimiter character (default: ',')\n");
    fprintf(stderr, "  -q, --quote=C       Set quote character (default: '\"')\n");
    fprintf(stderr, "  -e, --escape=C      Set escape character (default: '\\')\n");
    fprintf(stderr, "  -b, --block-size=SIZE Block size in KB (default: 1024)\n");
    fprintf(stderr, "  -r, --max-rows=N    Max rows per batch (default: 10000)\n");
    fprintf(stderr, "  --no-quoting        Disable quote handling\n");
    fprintf(stderr, "  --no-escaping       Disable escape handling\n");
    fprintf(stderr, "  --trim-whitespace   Trim whitespace from fields\n");
    fprintf(stderr, "  --ignore-empty      Ignore empty lines\n");
    fprintf(stderr, "  --no-parallel       Disable parallel processing\n");
    fprintf(stderr, "  --no-memory-pool    Disable memory pool\n");
    fprintf(stderr, "  --pool-size=SIZE    Initial memory pool size in MB (default: 64)\n");
    fprintf(stderr, "  -v, --verbose       Show detailed progress\n");
    fprintf(stderr, "  -h, --help          Show this help message\n");
    fprintf(stderr, "\nSonicSV Batching Mode with Built-in Threading:\n");
    
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
    g_config.config = csv_default_block_config();
    g_config.config.num_threads = sysconf(_SC_NPROCESSORS_ONLN);
    g_config.config.parallel_processing = true;
    g_config.config.block_size = 1024 * 1024; // 1MB blocks
    g_config.config.max_rows_per_batch = 10000;
    g_config.config.use_memory_pool = true;
    g_config.config.initial_pool_size = 64 * 1024 * 1024; // 64MB
    g_config.rows = 0;
    g_config.fields = 0;
    g_config.batches = 0;
    
    // Command line options
    static struct option long_options[] = {
        {"threads",         required_argument, 0, 't'},
        {"delimiter",       required_argument, 0, 'd'},
        {"quote",           required_argument, 0, 'q'},
        {"escape",          required_argument, 0, 'e'},
        {"block-size",      required_argument, 0, 'b'},
        {"max-rows",        required_argument, 0, 'r'},
        {"no-quoting",      no_argument,       0, 1000},
        {"no-escaping",     no_argument,       0, 1001},
        {"trim-whitespace", no_argument,       0, 1002},
        {"ignore-empty",    no_argument,       0, 1003},
        {"no-parallel",     no_argument,       0, 1004},
        {"no-memory-pool",  no_argument,       0, 1005},
        {"pool-size",       required_argument, 0, 1006},
        {"verbose",         no_argument,       0, 'v'},
        {"help",            no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "t:d:q:e:b:r:vh", long_options, &option_index)) != -1) {
        switch (opt) {
            case 't':
                g_config.config.num_threads = atoi(optarg);
                if (g_config.config.num_threads < 1) {
                    fprintf(stderr, "-w-- Invalid thread count, using 1\n");
                    g_config.config.num_threads = 1;
                }
                break;
            case 'd':
                g_config.config.parse_options.delimiter = optarg[0];
                break;
            case 'q':
                g_config.config.parse_options.quote_char = optarg[0];
                break;
            case 'e':
                g_config.config.parse_options.escape_char = optarg[0];
                break;
            case 'b':
                g_config.config.block_size = atoi(optarg) * 1024;
                if (g_config.config.block_size < 4096) {
                    fprintf(stderr, "-w-- Block size too small, using 4KB\n");
                    g_config.config.block_size = 4096;
                }
                break;
            case 'r':
                g_config.config.max_rows_per_batch = atoi(optarg);
                if (g_config.config.max_rows_per_batch < 100) {
                    fprintf(stderr, "-w-- Max rows too small, using 100\n");
                    g_config.config.max_rows_per_batch = 100;
                }
                break;
            case 1000:
                g_config.config.parse_options.quoting = false;
                break;
            case 1001:
                g_config.config.parse_options.escaping = false;
                break;
            case 1002:
                g_config.config.parse_options.trim_whitespace = true;
                break;
            case 1003:
                g_config.config.parse_options.ignore_empty_lines = true;
                break;
            case 1004:
                g_config.config.parallel_processing = false;
                g_config.config.num_threads = 1;
                break;
            case 1005:
                g_config.config.use_memory_pool = false;
                break;
            case 1006:
                g_config.config.initial_pool_size = atoi(optarg) * 1024 * 1024;
                if (g_config.config.initial_pool_size < 1024 * 1024) {
                    fprintf(stderr, "-w-- Pool size too small, using 1MB\n");
                    g_config.config.initial_pool_size = 1024 * 1024;
                }
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
    printf("-i-- Parsing mode: SonicSV Batching (%s)\n", 
           g_config.config.parallel_processing ? "multi-threaded" : "single-threaded");
    printf("-i-- File: %s (%.2f MB)\n", filename, file_size / (1024.0 * 1024.0));
    printf("-i-- Threads: %u, Block size: %.2f KB, Max rows/batch: %u\n", 
           g_config.config.num_threads, g_config.config.block_size / 1024.0, 
           g_config.config.max_rows_per_batch);
    printf("-i-- Options: delimiter='%c', quote='%c', escape='%c'\n", 
           g_config.config.parse_options.delimiter, g_config.config.parse_options.quote_char, 
           g_config.config.parse_options.escape_char);
    printf("-i-- Memory pool: %s (%.2f MB), Parallel: %s\n",
           g_config.config.use_memory_pool ? "enabled" : "disabled",
           g_config.config.initial_pool_size / (1024.0 * 1024.0),
           g_config.config.parallel_processing ? "enabled" : "disabled");
    
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
    
    // Create CSV block parser
    csv_block_parser_t* parser = csv_block_parser_create(&g_config.config);
    if (!parser) {
        fprintf(stderr, "-e-- Failed to create CSV block parser\n");
        return 1;
    }
    
    // Set batch callback
    csv_block_parser_set_batch_callback(parser, batch_callback, &g_config);
    
    // Start timing
    printf("-i-- Starting CSV parsing...\n");
    double t0 = now_sec();
    
    // Parse the file
    csv_error_t parse_error = csv_block_parse_file(parser, filename);
    
    double total_time = now_sec() - t0;
    
    if (g_config.verbose) {
        printf("\n");
    }
    
    // Print results
    if (parse_error == CSV_OK && !csv_abort_requested) {
        double throughput = file_size / (1024.0 * 1024.0) / total_time;
        
        printf("-s-- Parsing completed successfully\n");
        printf("-i-- Parsed %lu lines (%lu fields) in %lu batches in %.6f seconds (%.2e lines/sec)\n", 
               g_config.rows, g_config.fields, g_config.batches, total_time, g_config.rows / total_time);
        printf("-i-- Throughput: %.2f MB/s\n", throughput);
        printf("-i-- Average fields per line: %.1f\n", 
               g_config.rows > 0 ? (double)g_config.fields / g_config.rows : 0.0);
        printf("-i-- Average rows per batch: %.1f\n",
               g_config.batches > 0 ? (double)g_config.rows / g_config.batches : 0.0);
        
        // Print detailed stats
        csv_advanced_stats_t stats = csv_block_parser_get_stats(parser);
        printf("-i-- Advanced stats:\n");
        printf("-i--   Blocks processed: %lu, Batches created: %lu\n", 
               stats.total_blocks_processed, stats.total_batches_created);
        printf("-i--   SIMD operations: %lu, Parallel tasks: %lu\n",
               stats.total_simd_operations, stats.total_parallel_tasks);
        printf("-i--   Avg block parse time: %.3f ms, Avg batch creation time: %.3f ms\n",
               stats.avg_block_parse_time_ms, stats.avg_batch_creation_time_ms);
        printf("-i--   SIMD acceleration ratio: %.2fx, Peak memory usage: %u MB\n",
               stats.simd_acceleration_ratio, stats.peak_memory_usage_mb);
        printf("-i--   Threading efficiency: %.1f%% (%.1f parallel tasks per thread)\n",
               g_config.config.num_threads > 0 ? 
               (double)stats.total_parallel_tasks / g_config.config.num_threads / stats.total_blocks_processed * 100.0 : 0.0,
               g_config.config.num_threads > 0 ? 
               (double)stats.total_parallel_tasks / g_config.config.num_threads : 0.0);
        
    } else if (csv_abort_requested) {
        fprintf(stderr, "-w-- Parsing interrupted by user\n");
    } else {
        fprintf(stderr, "-e-- Parsing failed: %s\n", csv_error_string(parse_error));
    }
    
    // Cleanup
    csv_block_parser_destroy(parser);
    
    return (parse_error == CSV_OK && !csv_abort_requested) ? 0 : 1;
} 