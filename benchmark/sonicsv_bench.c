/**
 * SonicSV Benchmark Suite
 * 
 * Performance comparison between SonicSV (single and multi-threaded)
 * and LibCSV (single and multi-threaded).
 * 
 * GitHub: https://github.com/Vitruves/SonicSV
 * 
 * Copyright (c) 2023 SonicSV Contributors
 * MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdatomic.h>
#include <errno.h>
#include <csv.h>
#include "../sonicsv.h"
#include <float.h>  // For DBL_MAX
#include <math.h>  // For fmin, fmax

// =============== Global Variables ===============
#define MAX_THREADS 64
#define DEFAULT_BUFFER_SIZE (4 * 1024 * 1024)  // 4MB
#define BENCHMARK_ITERATIONS 5  // Number of times to run each benchmark

// Global variables for tracking progress
static volatile sig_atomic_t g_terminate = 0;
static atomic_size_t g_processed_rows = 0;
static size_t g_total_rows = 0;
static pthread_mutex_t g_progress_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_size_t g_total_fields = 0;

// =============== LibCSV Thread Work Structures ===============
typedef struct {
    const char* filename;
    long start_offset;
    long end_offset;
    size_t thread_id;
    size_t rows_processed;
    size_t fields_processed;
    struct csv_parser parser;
} LibCSVThreadWork;

// =============== Utility Functions ===============

// Function to get high-precision time
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

// Signal handler for clean termination
void handle_signal(int signal) {
    if (signal == SIGINT) {
        printf("\nInterrupt signal received. Cleaning up...\n");
        g_terminate = 1;
    }
}

// Function to get terminal width
int get_terminal_width() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col > 0 ? w.ws_col : 80;
}

// Function to print a smooth, colored progress bar
void print_progress_bar(double percent, size_t current, size_t total, const char* label) {
    pthread_mutex_lock(&g_progress_mutex);
    
    if (percent > 100.0) percent = 100.0;
    if (percent < 0.0) percent = 0.0;
    
    int width = get_terminal_width();
    char text[100];
    snprintf(text, sizeof(text), "] %.2f%% complete (%zu/%zu rows)", percent, current, total);
    int text_length = strlen(label) + 2 + strlen(text);
    int bar_width = width - text_length;
    
    if (bar_width < 10) bar_width = 10;
    
    int pos = (int)(bar_width * percent / 100.0);
    
    printf("\033[?25l"); // Hide cursor
    printf("\r\033[K");  // Clear line
    
    printf("%s: [", label);
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos) printf("\033[42m \033[0m"); // Green
        else if (i == pos) printf("\033[43m \033[0m"); // Yellow
        else printf(" ");
    }
    printf("%s", text);
    fflush(stdout);
    
    printf("\033[?25h"); // Show cursor
    
    pthread_mutex_unlock(&g_progress_mutex);
}

// Fast row counter using SonicSV 
size_t count_rows_fast(const char* filename) {
    SonicSVConfig config = sonicsv_default_config();
    config.use_mmap = true;
    
    long count = sonicsv_count_rows(filename, &config);
    if (count < 0) {
        fprintf(stderr, "Error counting rows: %s\n", sonicsv_get_error());
        return 0;
    }
    
    return (size_t)count;
}

// =============== SonicSV Benchmarking ===============

// Callback for SonicSV row processing
void sonicsv_process_row(const SonicSVRow* row, void* user_data) {
    (void)user_data; // Unused parameter
    atomic_fetch_add(&g_total_fields, sonicsv_row_get_field_count(row));
    atomic_fetch_add(&g_processed_rows, 1);
    
    if (g_terminate) return;
}

// SonicSV single-threaded benchmark
void benchmark_sonicsv_single(const char* filename) {
    printf("Running SonicSV single-threaded benchmark...\n");
    
    // Reset counters
    atomic_store(&g_processed_rows, 0);
    atomic_store(&g_total_fields, 0);
    
    // Configure SonicSV
    SonicSVConfig config = sonicsv_default_config();
    config.use_mmap = true;  // Use memory mapping for performance
    config.buffer_size = DEFAULT_BUFFER_SIZE;
    
    // Open the CSV file
    SonicSV* stream = sonicsv_open(filename, &config);
    if (!stream) {
        printf("Error opening file with SonicSV: %s\n", sonicsv_get_error());
        return;
    }
    
    // Process rows
    SonicSVRow* row;
    while ((row = sonicsv_next_row(stream)) != NULL && !g_terminate) {
        atomic_fetch_add(&g_total_fields, sonicsv_row_get_field_count(row));
        atomic_fetch_add(&g_processed_rows, 1);
        sonicsv_row_free(row);
    }
    
    // Clean up
    sonicsv_close(stream);
}

// SonicSV multi-threaded benchmark
void benchmark_sonicsv_multi(const char* filename) {
    printf("Running SonicSV multi-threaded benchmark...\n");
    
    // Reset counters
    atomic_store(&g_processed_rows, 0);
    atomic_store(&g_total_fields, 0);
    
    // Configure SonicSV
    SonicSVConfig config = sonicsv_default_config();
    config.use_mmap = true;
    config.buffer_size = DEFAULT_BUFFER_SIZE;
    
    // Use specified thread count or optimal count
    size_t thread_count = sonicsv_get_optimal_threads();
    printf("  Using %zu threads\n", thread_count);
    
    if (!sonicsv_thread_pool_init(thread_count)) {
        printf("  Error initializing thread pool: %s\n", sonicsv_get_error());
        return;
    }
    
    // Run the multi-threaded benchmark
    long rows = sonicsv_process_parallel(filename, &config, sonicsv_process_row, NULL);
    if (rows < 0) {
        printf("Error processing file: %s\n", sonicsv_get_error());
    }
    
    // Clean up
    sonicsv_thread_pool_destroy();
}

// =============== LibCSV Benchmarking ===============

// LibCSV callbacks
void libcsv_field_cb(void* data, size_t field_len, void* context) {
    (void)data;
    (void)field_len;
    (void)context;
    atomic_fetch_add(&g_total_fields, 1);
}

void libcsv_row_cb(int delim, void* context) {
    (void)delim;
    (void)context;
    atomic_fetch_add(&g_processed_rows, 1);
}

// LibCSV single-threaded benchmark
void benchmark_libcsv_single(const char* filename) {
    printf("Running LibCSV single-threaded benchmark...\n");
    
    // Reset counters
    atomic_store(&g_processed_rows, 0);
    atomic_store(&g_total_fields, 0);
    
    // Initialize the parser
    struct csv_parser parser;
    if (csv_init(&parser, CSV_APPEND_NULL) != 0) {
        printf("Error initializing LibCSV parser\n");
        return;
    }
    
    // Open the file
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("Error opening file: %s\n", strerror(errno));
        csv_free(&parser);
        return;
    }
    
    // Process the file
    char* buffer = malloc(DEFAULT_BUFFER_SIZE);
    if (!buffer) {
        printf("Error allocating buffer memory\n");
        fclose(file);
        csv_free(&parser);
        return;
    }
    
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, DEFAULT_BUFFER_SIZE, file)) > 0 && !g_terminate) {
        if (csv_parse(&parser, buffer, bytes_read, libcsv_field_cb, libcsv_row_cb, NULL) != bytes_read) {
            printf("Error parsing CSV: %s\n", csv_strerror(csv_error(&parser)));
            break;
        }
    }
    
    // Finalize parsing
    csv_fini(&parser, libcsv_field_cb, libcsv_row_cb, NULL);
    
    // Clean up
    free(buffer);
    fclose(file);
    csv_free(&parser);
}

// Thread worker function for LibCSV
void* libcsv_thread_worker(void* arg) {
    LibCSVThreadWork* work = (LibCSVThreadWork*)arg;
    
    FILE* file = fopen(work->filename, "rb"); // Open in binary mode for precise seeking
    if (!file) {
        fprintf(stderr, "LibCSV Thread %zu: Error opening file %s: %s\n", work->thread_id, work->filename, strerror(errno));
        return NULL;
    }
    
    if (fseek(file, work->start_offset, SEEK_SET) != 0) {
        fprintf(stderr, "LibCSV Thread %zu: Error seeking to %ld: %s\n", work->thread_id, work->start_offset, strerror(errno));
        fclose(file);
        return NULL;
    }
    
    // If not the first thread, skip to the end of the current line to start processing from a fresh line.
    // This helps avoid parsing partial lines from the previous chunk.
    if (work->start_offset > 0) {
        int c;
        while ((c = fgetc(file)) != EOF && c != '\n') {
            // Discard characters until a newline or EOF
        }
        if (c == EOF && ferror(file)) {
            fprintf(stderr, "LibCSV Thread %zu: Error skipping to newline: %s\n", work->thread_id, strerror(errno));
            fclose(file);
            return NULL;
        }
    }
    
    char* buffer = malloc(DEFAULT_BUFFER_SIZE);
    if (!buffer) {
        fprintf(stderr, "LibCSV Thread %zu: Error allocating buffer\n", work->thread_id);
        fclose(file);
        return NULL;
    }
    
    long current_file_pos = ftell(file);
    if (current_file_pos == -1L) {
        fprintf(stderr, "LibCSV Thread %zu: Error ftell after seek/skip: %s\n", work->thread_id, strerror(errno));
        free(buffer);
        fclose(file);
        return NULL;
    }

    // Process data strictly within the [current_file_pos, work->end_offset) range
    while (current_file_pos < work->end_offset && !g_terminate) {
        size_t bytes_to_attempt_read = DEFAULT_BUFFER_SIZE;
        
        // Calculate how many bytes are left in this thread's designated chunk
        long remaining_in_chunk = work->end_offset - current_file_pos;
        if (remaining_in_chunk <= 0) break; // No more data in this chunk

        if (bytes_to_attempt_read > (size_t)remaining_in_chunk) {
            bytes_to_attempt_read = (size_t)remaining_in_chunk;
        }

        if (bytes_to_attempt_read == 0) break; // Should not happen if remaining_in_chunk > 0

        size_t bytes_actually_read = fread(buffer, 1, bytes_to_attempt_read, file);

        if (bytes_actually_read == 0) {
            if (ferror(file)) {
                fprintf(stderr, "LibCSV Thread %zu: File read error: %s\n", work->thread_id, strerror(errno));
            }
            break; // EOF or error
        }

        // csv_parse processes the buffer.
        // It's crucial that libcsv itself handles partial records at the end of `bytes_actually_read` correctly
        // if `bytes_actually_read` means the chunk boundary is mid-record.
        // This external chunking is the main weakness of parallelizing libcsv this way.
        size_t bytes_parsed = csv_parse(&work->parser, buffer, bytes_actually_read, libcsv_field_cb, libcsv_row_cb, NULL);
        
        if (bytes_parsed != bytes_actually_read) {
            // This often means the buffer ended mid-record, or an error occurred.
            // libcsv might not consume the whole buffer if it stops on a parsing error or needs more data for a complete record.
            if (csv_error(&work->parser) != 0 && csv_error(&work->parser) != CSV_EPARSE /* CSV_EPARSE means "Premature end of record" */) {
                 fprintf(stderr, "LibCSV Thread %zu: Parsing error: %s (Code: %d)\n", work->thread_id, csv_strerror(csv_error(&work->parser)), csv_error(&work->parser));
            }
            // We must advance current_file_pos by what was *consumed from the file stream* (bytes_actually_read),
            // not by bytes_parsed, to avoid getting stuck or re-reading.
        }
        current_file_pos += bytes_actually_read;
    }
    
    // Finalize parsing for this thread. This should process any remaining buffered data in the parser
    // for the last record in this thread's chunk.
    csv_fini(&work->parser, libcsv_field_cb, libcsv_row_cb, NULL);
    
    free(buffer);
    fclose(file);
    
    return NULL;
}

// LibCSV multi-threaded benchmark
void benchmark_libcsv_multi(const char* filename) {
    printf("Running LibCSV multi-threaded benchmark...\n");
    
    // Reset counters
    atomic_store(&g_processed_rows, 0);
    atomic_store(&g_total_fields, 0);
    
    // Get file size
    struct stat st;
    if (stat(filename, &st) != 0) {
        printf("Error getting file size: %s\n", strerror(errno));
        return;
    }
    
    // Determine thread count
    size_t thread_count = sysconf(_SC_NPROCESSORS_ONLN);
    if (thread_count > MAX_THREADS) thread_count = MAX_THREADS;
    if (thread_count < 1) thread_count = 1;
    
    printf("  Using %zu threads\n", thread_count);
    
    // Create threads and work units with thread-local counters
    pthread_t threads[MAX_THREADS];
    LibCSVThreadWork* work = calloc(thread_count, sizeof(LibCSVThreadWork));
    if (!work) {
        printf("Error allocating thread work memory\n");
        return;
    }
    
    // Calculate chunk size
    size_t chunk_size = st.st_size / thread_count;
    
    // Create and start threads
    for (size_t i = 0; i < thread_count; i++) {
        work[i].filename = filename;
        work[i].thread_id = i;
        work[i].start_offset = i * chunk_size;
        work[i].end_offset = (i == thread_count - 1) ? (long)st.st_size : (long)((i + 1) * chunk_size);
        // work[i].rows_processed = 0; // Not used for global count directly
        // work[i].fields_processed = 0; // Not used for global count directly
        
        if (csv_init(&work[i].parser, CSV_APPEND_NULL) != 0) {
            printf("Error initializing parser for thread %zu\n", i);
            // Properly clean up already allocated resources if one fails
            for(size_t k=0; k<i; ++k) csv_free(&work[k].parser);
            free(work);
            return;
        }
        
        if (pthread_create(&threads[i], NULL, libcsv_thread_worker, &work[i]) != 0) {
            printf("Error creating thread %zu\n", i);
            csv_free(&work[i].parser); // Free parser for this failed thread
            // Join successfully created threads before exiting
            for(size_t k=0; k<i; ++k) pthread_join(threads[k], NULL);
            // Free parsers for successfully created and joined threads
            for(size_t k=0; k<i; ++k) csv_free(&work[k].parser);
            free(work);
            return;
        }
    }
    
    // Wait for threads to complete
    for (size_t i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
        csv_free(&work[i].parser); // Free parser after thread finishes
    }
    
    free(work);
    
    // IMPORTANT: For LibCSV MT, the g_processed_rows and g_total_fields are summed
    // by the atomic callbacks. If these numbers are still wildly off (especially fields),
    // it indicates the chunking strategy fundamentally conflicts with how libcsv parses,
    // leading to records being partially parsed or fields miscounted across chunk boundaries.
    // The g_total_rows (from single-threaded count_rows_fast) is the expected row count.
    // If atomic_load(&g_processed_rows) for LibCSV MT doesn't match g_total_rows, it's a clear sign of issues.
    // The benchmark previously overwrote g_processed_rows with g_total_rows for LibCSV MT,
    // which hides whether the MT version actually counted correctly. Let's rely on the atomics.
}

// =============== Benchmark Functions ===============

// Run a specific benchmark and return elapsed time
double run_benchmark(void (*benchmark_func)(const char*), const char* filename, const char* label) {
    double start_time = get_time_ms();
    
    // Run the benchmark
    benchmark_func(filename);
    
    double end_time = get_time_ms();
    double time_taken = (end_time - start_time) / 1000.0;
    
    // Print results
    printf("%s Results:\n", label);
    printf("  Total rows: %zu\n", atomic_load(&g_processed_rows));
    printf("  Total fields: %zu\n", atomic_load(&g_total_fields));
    printf("  Time: %.2f seconds\n", time_taken);
    
    if (time_taken > 0) {
        double rows_per_second = atomic_load(&g_processed_rows) / time_taken;
        printf("  Speed: %.2f rows/second\n", rows_per_second);
        printf("  Avg fields per row: %.2f\n", 
               atomic_load(&g_processed_rows) > 0 ? 
               (double)atomic_load(&g_total_fields) / atomic_load(&g_processed_rows) : 0);
    }
    
    return time_taken;
}

// Structure to hold benchmark statistics
typedef struct {
    double min;
    double max;
    double avg;
    double times[BENCHMARK_ITERATIONS];
} BenchmarkStats;

// Run a specific benchmark multiple times and return statistics
BenchmarkStats run_benchmark_multiple(void (*benchmark_func)(const char*), const char* filename, const char* label) {
    BenchmarkStats stats = {
        .min = DBL_MAX,
        .max = 0,
        .avg = 0
    };
    
    printf("\nRunning %d iterations of %s...\n", BENCHMARK_ITERATIONS, label);
    
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        // Reset counters for each iteration
        atomic_store(&g_processed_rows, 0);
        atomic_store(&g_total_fields, 0);
        
        // Run the benchmark
        double time = run_benchmark(benchmark_func, filename, label);
        stats.times[i] = time;
        
        // Update statistics
        stats.min = fmin(stats.min, time);
        stats.max = fmax(stats.max, time);
        stats.avg += time;
        
        printf("  Iteration %d: %.2f seconds\n", i + 1, time);
    }
    
    stats.avg /= BENCHMARK_ITERATIONS;
    return stats;
}

// Print comparative results table with statistics
void print_comparison_table(const char* filename, BenchmarkStats* stats, const char** labels) {
    printf("\n-- Comparative Performance Results (%d iterations) --\n", BENCHMARK_ITERATIONS);
    printf("File: %s\n\n", filename);
    
    // Print table header
    printf("| %-20s | %-8s | %-8s | %-8s | %-15s |\n", 
           "Implementation", "Min(s)", "Avg(s)", "Max(s)", "Speedup vs LibCSV");
    printf("|----------------------|----------|----------|----------|------------------|\n");
    
    // Print results using LibCSV as baseline
    // Single-threaded comparison
    printf("| %-20s | %8.2f | %8.2f | %8.2f | %15.2fx |\n", 
           labels[2], stats[2].min, stats[2].avg, stats[2].max, 1.0);  // LibCSV ST baseline
    printf("| %-20s | %8.2f | %8.2f | %8.2f | %15.2fx |\n", 
           labels[0], stats[0].min, stats[0].avg, stats[0].max, 
           stats[2].avg / stats[0].avg);  // SonicSV ST
    
    // Multi-threaded comparison
    printf("| %-20s | %8.2f | %8.2f | %8.2f | %15.2fx |\n", 
           labels[3], stats[3].min, stats[3].avg, stats[3].max, 1.0);  // LibCSV MT baseline
    printf("| %-20s | %8.2f | %8.2f | %8.2f | %15.2fx |\n", 
           labels[1], stats[1].min, stats[1].avg, stats[1].max, 
           stats[3].avg / stats[1].avg);  // SonicSV MT
    
    printf("\n");
}

// =============== Main Function ===============
int main(int argc, char** argv) {
    // Parse command line arguments
    if (argc < 2) {
        printf("Usage: %s -i/--input <csv_file> [-j/--jobs <threads>] [-h/--help]\n", argv[0]);
        return 1;
    }
    
    // Register signal handler
    signal(SIGINT, handle_signal);
    
    // Default values
    const char* filename = NULL;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("SonicSV Benchmark Suite\n\n");
            printf("Usage: %s -i/--input <csv_file> [-j/--jobs <threads>] [-h/--help]\n\n", argv[0]);
            printf("Options:\n");
            printf("  -i, --input <file>    CSV file to benchmark\n");
            printf("  -j, --jobs <threads>  Number of threads (default: auto-detect)\n");
            printf("  -h, --help            Display this help message\n");
            return 0;
        } else if ((strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) && i + 1 < argc) {
            filename = argv[++i];
        } else if ((strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "--jobs") == 0) && i + 1 < argc) {
            // Store thread count directly in SonicSV's thread pool 
            size_t thread_count = atoi(argv[++i]);
            if (thread_count > 0) {
                printf("Using specified thread count: %zu\n", thread_count);
                // Will be used when initializing thread pools
                sonicsv_thread_pool_init(thread_count);
            }
        }
    }
    
    if (!filename) {
        printf("Error: No input file specified\n");
        return 1;
    }
    
    // Print benchmark header
    printf("-- CSV Library Benchmark Suite --\n");
    printf("=================================\n");
    printf("Testing file: %s\n", filename);
    
    // Get file information
    struct stat st;
    if (stat(filename, &st) == 0) {
        printf("File size: %.2f MB\n", st.st_size / (1024.0 * 1024.0));
    }
    
    // Count total rows for tracking
    printf("Counting rows for benchmarking...\n");
    g_total_rows = count_rows_fast(filename);
    printf("Found %zu rows\n", g_total_rows);
    
    // Perform benchmarks
    const char* labels[4] = {
        "SonicSV [ST]", "SonicSV [MT]", 
        "LibCSV [ST]", "LibCSV [MT]"
    };
    BenchmarkStats stats[4];
    
    // Run all benchmarks multiple times
    stats[0] = run_benchmark_multiple(benchmark_sonicsv_single, filename, labels[0]);
    stats[1] = run_benchmark_multiple(benchmark_sonicsv_multi, filename, labels[1]);
    stats[2] = run_benchmark_multiple(benchmark_libcsv_single, filename, labels[2]);
    stats[3] = run_benchmark_multiple(benchmark_libcsv_multi, filename, labels[3]);
    
    // Print comparison table with statistics
    print_comparison_table(filename, stats, labels);
    
    return 0;
}