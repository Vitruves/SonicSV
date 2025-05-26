#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <getopt.h>
#include <signal.h>

/* Include csv.h from one of the possible locations */
#if defined(HAVE_LIBFCCP_CSV_H)
#include <libfccp/csv.h>
#else
#include <csv.h>
#endif

/* If using a custom libcsv or need to add function declarations */
#ifndef CSV_MAJOR
#define CSV_MAJOR 3
#define CSV_MINOR 0
#define CSV_RELEASE 3

/* Provide function declarations if they're missing */
int csv_init(void *p, unsigned char options);
void csv_free(void *p);
size_t csv_parse(void *p, const void *s, size_t len, 
                void (*cb1)(void *, size_t, void *),
                void (*cb2)(int, void *), 
                void *data);
int csv_fini(void *p, 
            void (*cb1)(void *, size_t, void *),
            void (*cb2)(int, void *), 
            void *data);
int csv_error(void *p);
char * csv_strerror(int error);
void csv_set_delim(void *p, unsigned char c);
void csv_set_quote(void *p, unsigned char c);
#endif

// Global variable for abort handling
volatile sig_atomic_t csv_abort_requested;

// Simple structure to track benchmark data
typedef struct {
    uint64_t rows;
    uint64_t fields;
    double throughput;
    bool success;
    bool verbose;
} BenchmarkData;

// Get current time in seconds with high precision
static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// Field callback
static void field_callback(void* field, size_t field_len, void* user_data) {
    (void)field;      // Unused parameter
    (void)field_len;  // Unused parameter
    BenchmarkData* data = (BenchmarkData*)user_data;
    data->fields++;
}

// Row callback
static void row_callback(int c, void* user_data) {
    (void)c;          // Unused parameter
    BenchmarkData* data = (BenchmarkData*)user_data;
    data->rows++;
    
    // Display progress if verbose
    if (data->verbose && data->rows % 100000 == 0) {
        fprintf(stderr, "\r-i-- Processing: %lu rows", data->rows);
    }
}

// Signal handler
static void csv_signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM)
        csv_abort_requested = 1;
}

// Set up signal handlers for graceful abort
static int enable_abort_handling(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = csv_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1 ||
        sigaction(SIGTERM, &sa, NULL) == -1) {
        return -1;
    }
    return 0;
}

// Disable signal handlers
static void disable_abort_handling(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    csv_abort_requested = 0;
}

// Usage information
void print_usage(const char* program_name) {
    fprintf(stderr, "Usage: %s [options] <csvfile>\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -d, --delimiter=C   Set delimiter character (default: ',')\n");
    fprintf(stderr, "  -q, --quote=C       Set quote character (default: '\"')\n");
    fprintf(stderr, "  -b, --buffer=SIZE   Set buffer size in KB (default: 256)\n");
    fprintf(stderr, "  -v, --verbose       Show detailed progress\n");
    fprintf(stderr, "  -h, --help          Show this help message\n");
}

int main(int argc, char** argv) {
    // Default options - optimized for performance
    char delimiter = ',';
    char quote = '"';
    bool verbose = false;
    size_t buffer_size = 2 * 1024 * 1024; // 2MB default for better performance
    
    // Command line options
    static struct option long_options[] = {
        {"delimiter", required_argument, 0, 'd'},
        {"quote",     required_argument, 0, 'q'},
        {"buffer",    required_argument, 0, 'b'},
        {"verbose",   no_argument,       0, 'v'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "d:q:b:vh", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                delimiter = optarg[0];
                break;
            case 'q':
                quote = optarg[0];
                break;
            case 'b':
                buffer_size = atoi(optarg) * 1024;
                if (buffer_size < 4096) {
                    fprintf(stderr, "-e-- Buffer size too small, using 4KB\n");
                    buffer_size = 4096;
                }
                break;
            case 'v':
                verbose = true;
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
    size_t file_size = 0;
    if (stat(filename, &st) == 0) {
        file_size = (size_t)st.st_size;
    } else {
        fprintf(stderr, "-e-- Cannot determine file size for %s\n", filename);
        return 1;
    }
    
    if (file_size == 0) {
        fprintf(stderr, "-e-- File is empty: %s\n", filename);
        return 1;
    }
    
    // Initialize benchmarking data
    BenchmarkData data = {0};
    data.verbose = verbose;
    
    // Initialize CSV parser with void pointer to avoid struct definition issues
    void* parser = malloc(4096); // Allocate enough space for any parser implementation
    if (!parser) {
        fprintf(stderr, "-e-- Failed to allocate parser memory\n");
        return 1;
    }
    
    if (csv_init(parser, 0) != 0) {
        fprintf(stderr, "-e-- Failed to initialize CSV parser\n");
        free(parser);
        return 1;
    }
    
    // Set parser options
    csv_set_delim(parser, delimiter);
    csv_set_quote(parser, quote);
    
    // Open the file
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "-e-- Failed to open file: %s\n", filename);
        csv_free(parser);
        free(parser);
        return 1;
    }
    
    // Enable abort handling (CTRL+C)
    enable_abort_handling();
    
    // Display parsing info
    printf("-i-- Opening file: %s (%.2f MB)\n", filename, file_size / (1024.0 * 1024.0));
    printf("-i-- Parsing with system libcsv, delimiter: '%c', quote: '%c', buffer: %.2f KB\n", 
           delimiter, quote, buffer_size / 1024.0);
    
    // Time the parsing
    double t0 = now_sec();
    
    // Allocate read buffer
    char* buffer = (char*)malloc(buffer_size);
    if (!buffer) {
        fprintf(stderr, "-e-- Failed to allocate buffer memory\n");
        fclose(fp);
        csv_free(parser);
        free(parser);
        disable_abort_handling();
        return 1;
    }
    
    // Process the file
    size_t bytes_read;
    bool parse_success = true;
    
    while ((bytes_read = fread(buffer, 1, buffer_size, fp)) > 0) {
        // Check for abort request
        if (csv_abort_requested) {
            fprintf(stderr, "\n-e-- Parsing aborted by user\n");
            parse_success = false;
            break;
        }
        
        // Parse this chunk
        if (csv_parse(parser, buffer, bytes_read, field_callback, row_callback, &data) != bytes_read) {
            fprintf(stderr, "\n-e-- Error parsing CSV: %s\n", 
                    csv_strerror(csv_error(parser)));
            parse_success = false;
            break;
        }
    }
    
    // Check for file read error
    if (ferror(fp)) {
        fprintf(stderr, "\n-e-- Error reading file\n");
        parse_success = false;
    }
    
    // Finalize parsing if successful so far
    if (parse_success) {
        if (csv_fini(parser, field_callback, row_callback, &data) != 0) {
            fprintf(stderr, "\n-e-- Error finalizing CSV parsing: %s\n", 
                    csv_strerror(csv_error(parser)));
            parse_success = false;
        }
    }
    
    double t1 = now_sec();
    double elapsed = t1 - t0;
    
    // Clear the progress line if verbose
    if (verbose) {
        fprintf(stderr, "\r                                        \r");
    }
    
    // Print results
    if (parse_success) {
        data.success = true;
        data.throughput = file_size / (1024.0 * 1024.0) / elapsed;
        
        printf("-s-- Parsed %lu lines in %.6f seconds (%.2e lines/sec)\n", 
               data.rows, elapsed, data.rows / elapsed);
        printf("-s-- Throughput: %.2f MB/s\n", data.throughput);
        printf("-s-- Total fields: %lu (avg %.1f fields per row)\n", 
               data.fields, data.rows > 0 ? (double)data.fields / data.rows : 0);
        
        if (data.rows == 0) {
            printf("-w-- No data rows found in file\n");
        }
    } else {
        fprintf(stderr, "-e-- Failed to parse file\n");
    }
    
    // Clean up
    free(buffer);
    fclose(fp);
    csv_free(parser);
    free(parser);
    disable_abort_handling();
    
    return data.success ? 0 : 1;
} 