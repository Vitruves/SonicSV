#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <getopt.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <csv.h>

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
    char delimiter;
    bool verbose;
} ThreadData;

// Global configuration
typedef struct {
    int num_threads;
    bool verbose;
    char delimiter;
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

// Field callback for libcsv
static void field_callback(void *field, size_t field_len, void *user_data) {
    ThreadData* data = (ThreadData*)user_data;
    data->fields++;
}

// Row callback for libcsv
static void row_callback(int end_char, void *user_data) {
    ThreadData* data = (ThreadData*)user_data;
    data->rows++;
    
    if (csv_abort_requested) return;
    
    if (data->verbose && (data->rows % 50000 == 0)) {
        printf("\r[Thread %d] Processing rows: %lu", data->thread_id, data->rows);
        fflush(stdout);
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
    struct csv_parser parser;
    if (csv_init(&parser, CSV_APPEND_NULL) != 0) {
        thread_data->success = false;
        return NULL;
    }
    
    // Set delimiter
    csv_set_delim(&parser, thread_data->delimiter);
    
    // Parse the chunk
    size_t bytes_processed = csv_parse(&parser, thread_data->data_chunk, 
                                      thread_data->chunk_size, 
                                      field_callback, row_callback, thread_data);
    
    // Finalize parsing
    csv_fini(&parser, field_callback, row_callback, thread_data);
    
    thread_data->success = (bytes_processed == thread_data->chunk_size);
    thread_data->parse_time = now_sec() - t0;
    
    if (g_config.verbose && thread_data->success) {
        printf("\n[Thread %d] Parsed %lu rows (%lu fields) in %.3fs\n", 
               thread_data->thread_id, thread_data->rows, thread_data->fields, thread_data->parse_time);
    }
    
    csv_free(&parser);
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
    fprintf(stderr, "  -b, --buffer=SIZE   Set buffer size in KB (default: 2048)\n");
    fprintf(stderr, "  -v, --verbose       Show detailed progress\n");
    fprintf(stderr, "  -h, --help          Show this help message\n");
    fprintf(stderr, "\nMultithreaded libcsv Parser\n");
}

int main(int argc, char** argv) {
    // Default configuration
    g_config.num_threads = sysconf(_SC_NPROCESSORS_ONLN);
    g_config.verbose = false;
    g_config.delimiter = ',';
    g_config.buffer_size = 2048 * 1024; // 2MB default
    
    // Command line options
    static struct option long_options[] = {
        {"threads",    required_argument, 0, 't'},
        {"delimiter",  required_argument, 0, 'd'},
        {"buffer",     required_argument, 0, 'b'},
        {"verbose",    no_argument,       0, 'v'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "t:d:b:vh", long_options, &option_index)) != -1) {
        switch (opt) {
            case 't':
                g_config.num_threads = atoi(optarg);
                if (g_config.num_threads < 1) {
                    fprintf(stderr, "-w-- Invalid thread count, using 1\n");
                    g_config.num_threads = 1;
                }
                break;
            case 'd':
                g_config.delimiter = optarg[0];
                break;
            case 'b':
                g_config.buffer_size = atoi(optarg) * 1024;
                if (g_config.buffer_size < 4096) {
                    fprintf(stderr, "-w-- Buffer size too small, using 4KB\n");
                    g_config.buffer_size = 4096;
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
    printf("-i-- Parsing mode: Multithreaded libcsv, Threads: %d\n", g_config.num_threads);
    printf("-i-- File: %s (%.2f MB)\n", filename, file_size / (1024.0 * 1024.0));
    printf("-i-- Options: delimiter='%c', buffer=%.2f KB\n", 
           g_config.delimiter, g_config.buffer_size / 1024.0);
    
    // Read entire file into memory
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "-e-- Failed to open file: %s\n", filename);
        return 1;
    }
    
    char* file_data = malloc(file_size);
    if (!file_data) {
        fprintf(stderr, "-e-- Failed to allocate memory for file\n");
        fclose(fp);
        return 1;
    }
    
    if (fread(file_data, 1, file_size, fp) != file_size) {
        fprintf(stderr, "-e-- Failed to read file\n");
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
        fprintf(stderr, "-e-- Failed to allocate thread data\n");
        free(file_data);
        return 1;
    }
    
    // Set up thread chunks with proper line boundaries
    size_t current_offset = 0;
    for (int i = 0; i < g_config.num_threads; i++) {
        thread_data[i].thread_id = i;
        thread_data[i].start_offset = current_offset;
        thread_data[i].delimiter = g_config.delimiter;
        thread_data[i].verbose = g_config.verbose;
        
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
            printf("-i-- Thread %d: offset %zu, size %zu\n", i, 
                   thread_data[i].start_offset, thread_data[i].chunk_size);
        }
    }
    
    // Start timing
    printf("-i-- Starting CSV parsing...\n");
    double t0 = now_sec();
    
    // Create and start threads
    for (int i = 0; i < g_config.num_threads; i++) {
        if (pthread_create(&threads[i], NULL, thread_worker, &thread_data[i]) != 0) {
            fprintf(stderr, "-e-- Failed to create thread %d\n", i);
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
    
    if (g_config.verbose) {
        printf("\n");
    }
    
    // Print results
    if (all_success) {
        double throughput = file_size / (1024.0 * 1024.0) / total_time;
        
        printf("-s-- Parsing completed successfully\n");
        printf("-i-- Parsed %lu lines (%lu fields) in %.6f seconds (%.2e lines/sec)\n", 
               total_rows, total_fields, total_time, total_rows / total_time);
        printf("-i-- Throughput: %.2f MB/s\n", throughput);
        printf("-i-- Average fields per line: %.1f\n", 
               total_rows > 0 ? (double)total_fields / total_rows : 0.0);
        printf("-i-- Thread efficiency: %.1f%% (%.3fs max thread vs %.3fs total)\n",
               (max_thread_time / total_time) * 100.0, max_thread_time, total_time);
    } else {
        fprintf(stderr, "-e-- Some threads failed to parse their chunks\n");
    }
    
    // Cleanup
    free(file_data);
    free(thread_data);
    free(threads);
    
    return all_success ? 0 : 1;
} 