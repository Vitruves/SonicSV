/**
 * SonicSV - Multithreaded CSV Processing Example
 * 
 * This example demonstrates parallel processing of CSV files using
 * all available CPU cores for maximum performance.
 * 
 * Copyright (c) 2025 SonicSV Contributors
 * MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdatomic.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "../sonicsv.h"

// Global tracking variables
static volatile bool g_terminate = false;
static atomic_size_t g_total_rows_processed = 0;
static size_t g_total_rows = 0;
static pthread_mutex_t g_progress_mutex = PTHREAD_MUTEX_INITIALIZER;

// Thread stats structure
typedef struct {
    atomic_size_t fields_processed;
} ThreadStats;

// Signal handler
void handle_signal(int signal) {
    if (signal == SIGINT) {
        printf("\nInterrupt received. Cleaning up...\n");
        g_terminate = true;
    }
}

// Get terminal width
int get_terminal_width() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col > 0 ? w.ws_col : 80;
}

// Progress bar
void print_progress_bar(double percent, size_t current, size_t total) {
    pthread_mutex_lock(&g_progress_mutex);
    
    percent = percent > 100.0 ? 100.0 : (percent < 0.0 ? 0.0 : percent);
    
    int width = get_terminal_width();
    char text[100];
    snprintf(text, sizeof(text), "] %.2f%% complete (%zu/%zu rows)", percent, current, total);
    int text_length = strlen("Processing: [") + strlen(text);
    int bar_width = width - text_length;
    
    if (bar_width < 10) bar_width = 10;
    int pos = (int)(bar_width * percent / 100.0);
    
    printf("\033[?25l\r\033[K");
    printf("Processing: [");
    
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos) printf("\033[42m \033[0m");
        else if (i == pos) printf("\033[43m \033[0m");
        else printf(" ");
    }
    
    printf("%s", text);
    fflush(stdout);
    printf("\033[?25h");
    
    pthread_mutex_unlock(&g_progress_mutex);
}

// Row processing callback
void process_row(const SonicSVRow* row, void* user_data) {
    if (g_terminate) return;
    
    ThreadStats* stats = (ThreadStats*)user_data;
    size_t field_count = sonicsv_row_get_field_count(row);
    atomic_fetch_add(&stats->fields_processed, field_count);
    
    size_t total_processed = atomic_fetch_add(&g_total_rows_processed, 1) + 1;
    
    if (total_processed % 10000 == 0 || total_processed == g_total_rows) {
        double percent = (g_total_rows > 0) ? 
            (100.0 * (double)total_processed / (double)g_total_rows) : 0;
        print_progress_bar(percent, total_processed, g_total_rows);
    }
    
    // Process each field
    for (size_t i = 0; i < field_count; i++) {
        const char* field = sonicsv_row_get_field(row, i);
        if (field && *field) {
            // Simple string hash
            unsigned long hash = 5381;
            const char* p = field;
            int c;
            while ((c = *p++)) {
                hash = ((hash << 5) + hash) + c;
            }
            
            // Some CPU work
            for (int j = 0; j < 100; j++) {
                hash = hash * 1103515245 + 12345;
            }
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <csv_file> [num_threads] [delimiter]\n", argv[0]);
        return 1;
    }
    
    signal(SIGINT, handle_signal);
    const char* filename = argv[1];
    
    // Thread count
    int num_threads = 0;
    if (argc > 2) {
        num_threads = atoi(argv[2]);
        if (num_threads <= 0) {
            num_threads = 0; // Auto-detect
        }
    }
    
    // Get optimal thread count for display
    size_t optimal_threads = sonicsv_get_optimal_threads();
    printf("Using %zu processor cores for multithreading.\n", optimal_threads);
    
    // Configure SonicSV
    SonicSVConfig config = sonicsv_default_config();
    
    if (argc > 3 && argv[3][0]) {
        config.delimiter = argv[3][0];
    }
    
    // Memory mapping for large files
    config.use_mmap = true;
    
    printf("SonicSV Multi-threaded Processing Example --\n");
    printf("File: %s\n", filename);
    printf("Using %d threads (CPU cores)\n", 
           num_threads > 0 ? num_threads : (int)optimal_threads);
    printf("Delimiter: '%c'\n", config.delimiter);
    
    // Get file size
    struct stat st;
    if (stat(filename, &st) != 0) {
        printf("Error: Cannot access file\n");
        return 1;
    }
    printf("File size: %.2f MB\n", st.st_size / (1024.0 * 1024.0));
    
    // Single-threaded row counting first
    printf("Counting rows to optimize work distribution...\n");
    struct timeval count_start, count_end;
    gettimeofday(&count_start, NULL);
    
    long row_count = sonicsv_count_rows(filename, &config);
    if (row_count < 0) {
        printf("Error counting rows: %s\n", 
               sonicsv_get_error() ? sonicsv_get_error() : "Unknown error");
        return 1;
    }
    g_total_rows = (size_t)row_count;
    
    gettimeofday(&count_end, NULL);
    double counting_time = (count_end.tv_sec - count_start.tv_sec) + 
                         (count_end.tv_usec - count_start.tv_usec) / 1000000.0;
    
    printf("Found %zu rows in %.2f seconds (%.2f rows/sec)\n", 
           g_total_rows, counting_time, g_total_rows / counting_time);
    
    // Initialize thread pool
    if (!sonicsv_thread_pool_init(num_threads)) {
        printf("Error initializing thread pool: %s\n", sonicsv_get_error());
        return 1;
    }
    
    // Stats initialization
    ThreadStats stats;
    atomic_init(&stats.fields_processed, 0);
    
    // Reset progress counter
    atomic_store(&g_total_rows_processed, 0);
    g_terminate = false;
    
    // Start processing timer
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    
    // Process in parallel
    printf("\nStarting parallel processing...\n");
    long rows_processed = sonicsv_process_parallel(filename, &config, process_row, &stats);
    
    // Stop timer
    gettimeofday(&end_time, NULL);
    double total_time = (end_time.tv_sec - start_time.tv_sec) + 
                      (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    
    printf("\n\nProcessing Results --\n");
    
    if (rows_processed < 0) {
        printf("Error during processing: %s\n", 
               sonicsv_get_error() ? sonicsv_get_error() : "Unknown error");
        sonicsv_thread_pool_destroy();
        return 1;
    }
    
    // Results
    printf("Total rows processed: %ld\n", rows_processed);
    printf("Total fields processed: %zu\n", atomic_load(&stats.fields_processed));
    printf("Avg fields per row: %.2f\n", 
           rows_processed > 0 ? (double)atomic_load(&stats.fields_processed) / rows_processed : 0);
    printf("Total processing time: %.2f seconds\n", total_time);
    printf("Effective processing speed: %.2f rows/second\n", rows_processed / total_time);
    
    // Clean up
    sonicsv_thread_pool_destroy();
    
    return 0;
}