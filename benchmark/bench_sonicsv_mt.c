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
#include <pthread.h>
#include <unistd.h>

#include "../include/sonicsv.h"

// Thread data structure
typedef struct {
    int thread_id;
    char* data_chunk;
    size_t chunk_size;
    size_t start_offset;
    uint64_t rows;
    uint64_t fields;
    double parse_time;
    bool success;
} ThreadData;

// Global configuration
typedef struct {
    int num_threads;
    bool verbose;
    csv_parse_options_t options;
    size_t buffer_size;
} Config;

static Config g_config;
static volatile sig_atomic_t csv_abort_requested = 0;

// Get current time in seconds with high precision
static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// Row callback for SonicSV
static void row_callback(const csv_row_t* row, void* user_data) {
    ThreadData* data = (ThreadData*)user_data;
    data->rows++;
    data->fields += row->num_fields;
}

// Error callback for SonicSV
static void error_callback(csv_error_t error, const char* message, uint64_t row_number, void* user_data) {
    ThreadData* data = (ThreadData*)user_data;
    if (g_config.verbose) {
        fprintf(stderr, "\n[Thread %d] CSV Error at row %lu: %s (%s)\n", 
                data->thread_id, row_number, message, csv_error_string(error));
    }
}

// Find line boundaries to ensure we don't split CSV rows
static size_t find_line_boundary(const char* data, size_t start, size_t max_size) {
    if (start >= max_size) return max_size;
    
    // Look backwards from the target position to find a newline
    for (size_t i = start; i > 0; i--) {
        if (data[i] == '\n') {
            return i + 1; // Return position after the newline
        }
    }
    return 0; // If no newline found, start from beginning
}

// Thread worker function
static void* thread_worker(void* arg) {
    ThreadData* thread_data = (ThreadData*)arg;
    
    double t0 = now_sec();
    
    // Create CSV parser for this thread
    csv_parser_t* parser = csv_parser_create(&g_config.options);
    if (!parser) {
        thread_data->success = false;
        return NULL;
    }
    
    // Set callbacks
    csv_parser_set_row_callback(parser, row_callback, thread_data);
    csv_parser_set_error_callback(parser, error_callback, thread_data);
    
    // Parse the chunk
    csv_error_t parse_error = csv_parse_buffer(parser, thread_data->data_chunk, 
                                              thread_data->chunk_size, true);
    
    thread_data->success = (parse_error == CSV_OK);
    thread_data->parse_time = now_sec() - t0;
    
    if (g_config.verbose && thread_data->success) {
        printf("[Thread %d] Parsed %lu rows (%lu fields) in %.3fs\n", 
               thread_data->thread_id, thread_data->rows, thread_data->fields, thread_data->parse_time);
    }
    
    csv_parser_destroy(parser);
    return NULL;
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
    fprintf(stderr, "  -b, --buffer=SIZE   Set buffer size in KB (default: 256)\n");
    fprintf(stderr, "  --no-quoting        Disable quote handling\n");
    fprintf(stderr, "  --no-escaping       Disable escape handling\n");
    fprintf(stderr, "  --trim-whitespace   Trim whitespace from fields\n");
    fprintf(stderr, "  --ignore-empty      Ignore empty lines\n");
    fprintf(stderr, "  -v, --verbose       Show detailed progress\n");
    fprintf(stderr, "  -h, --help          Show this help message\n");
    fprintf(stderr, "\nMultithreaded SonicSV with SIMD Features:\n");
    
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
    g_config.num_threads = sysconf(_SC_NPROCESSORS_ONLN);
    g_config.verbose = false;
    g_config.options = csv_default_options();
    g_config.buffer_size = 256 * 1024;
    
    // Command line options
    static struct option long_options[] = {
        {"threads",         required_argument, 0, 't'},
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
    
    while ((opt = getopt_long(argc, argv, "t:d:q:e:b:vh", long_options, &option_index)) != -1) {
        switch (opt) {
            case 't':
                g_config.num_threads = atoi(optarg);
                if (g_config.num_threads < 1) {
                    fprintf(stderr, "Error: Invalid thread count, using 1\n");
                    g_config.num_threads = 1;
                }
                break;
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
                    fprintf(stderr, "Warning: Buffer size too small, using 4KB\n");
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
        fprintf(stderr, "Error: Missing input CSV file\n");
        print_usage(argv[0]);
        return 1;
    }
    
    const char* filename = argv[optind];
    
    // Get file size
    struct stat st;
    if (stat(filename, &st) != 0) {
        fprintf(stderr, "Error: Cannot access file %s\n", filename);
        return 1;
    }
    
    size_t file_size = (size_t)st.st_size;
    if (file_size == 0) {
        fprintf(stderr, "Error: File is empty: %s\n", filename);
        return 1;
    }
    
    // Display parsing info
    printf("Parsing mode: Multithreaded SonicSV, Threads: %d\n", g_config.num_threads);
    printf("File: %s (%.2f MB)\n", filename, file_size / (1024.0 * 1024.0));
    printf("Options: delimiter='%c', quote='%c', escape='%c', buffer=%.2f KB\n", 
           g_config.options.delimiter, g_config.options.quote_char, 
           g_config.options.escape_char, g_config.buffer_size / 1024.0);
    
    uint32_t simd_features = csv_simd_get_features();
    printf("SIMD acceleration: ");
    if (simd_features & CSV_SIMD_SSE4_2) printf("SSE4.2 ");
    if (simd_features & CSV_SIMD_AVX2) printf("AVX2 ");
    if (simd_features & CSV_SIMD_NEON) printf("NEON ");
    if (simd_features == CSV_SIMD_NONE) printf("None");
    printf("\n");
    
    // Read entire file into memory
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Failed to open file: %s\n", filename);
        return 1;
    }
    
    char* file_data = malloc(file_size);
    if (!file_data) {
        fprintf(stderr, "Error: Failed to allocate memory for file\n");
        fclose(fp);
        return 1;
    }
    
    if (fread(file_data, 1, file_size, fp) != file_size) {
        fprintf(stderr, "Error: Failed to read file\n");
        free(file_data);
        fclose(fp);
        return 1;
    }
    fclose(fp);
    
    // Set up signal handling
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = csv_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // Calculate chunk sizes
    size_t chunk_size = file_size / g_config.num_threads;
    ThreadData* thread_data = calloc(g_config.num_threads, sizeof(ThreadData));
    pthread_t* threads = malloc(g_config.num_threads * sizeof(pthread_t));
    
    if (!thread_data || !threads) {
        fprintf(stderr, "Error: Failed to allocate thread data\n");
        free(file_data);
        return 1;
    }
    
    // Set up thread chunks with proper line boundaries
    size_t current_offset = 0;
    for (int i = 0; i < g_config.num_threads; i++) {
        thread_data[i].thread_id = i;
        thread_data[i].start_offset = current_offset;
        
        if (i == g_config.num_threads - 1) {
            // Last thread gets remaining data
            thread_data[i].chunk_size = file_size - current_offset;
        } else {
            // Find proper line boundary
            size_t target_end = current_offset + chunk_size;
            size_t actual_end = find_line_boundary(file_data, target_end, file_size);
            thread_data[i].chunk_size = actual_end - current_offset;
        }
        
        thread_data[i].data_chunk = file_data + current_offset;
        current_offset += thread_data[i].chunk_size;
        
        if (g_config.verbose) {
            printf("Thread %d: offset %zu, size %zu\n", i, 
                   thread_data[i].start_offset, thread_data[i].chunk_size);
        }
    }
    
    // Start timing
    double t0 = now_sec();
    
    // Create and start threads
    for (int i = 0; i < g_config.num_threads; i++) {
        if (pthread_create(&threads[i], NULL, thread_worker, &thread_data[i]) != 0) {
            fprintf(stderr, "Error: Failed to create thread %d\n", i);
            // Continue with fewer threads
            g_config.num_threads = i;
            break;
        }
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < g_config.num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double total_time = now_sec() - t0;
    
    // Aggregate results
    uint64_t total_rows = 0;
    uint64_t total_fields = 0;
    bool all_success = true;
    double max_thread_time = 0.0;
    
    for (int i = 0; i < g_config.num_threads; i++) {
        total_rows += thread_data[i].rows;
        total_fields += thread_data[i].fields;
        all_success &= thread_data[i].success;
        if (thread_data[i].parse_time > max_thread_time) {
            max_thread_time = thread_data[i].parse_time;
        }
    }
    
    // Print results
    if (all_success) {
        double throughput = file_size / (1024.0 * 1024.0) / total_time;
        
        printf("Parsed %lu lines (%lu fields) in %.6f seconds (%.2e lines/sec)\n", 
               total_rows, total_fields, total_time, total_rows / total_time);
        printf("Throughput: %.2f MB/s\n", throughput);
        printf("Average fields per line: %.1f\n", 
               total_rows > 0 ? (double)total_fields / total_rows : 0.0);
        printf("Thread efficiency: %.1f%% (%.3fs max thread vs %.3fs total)\n",
               (max_thread_time / total_time) * 100.0, max_thread_time, total_time);
    } else {
        fprintf(stderr, "Error: Some threads failed to parse their chunks\n");
    }
    
    // Cleanup
    free(file_data);
    free(thread_data);
    free(threads);
    
    return all_success ? 0 : 1;
} 