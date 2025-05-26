#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/stat.h>
#include <string.h>
#include <stdint.h>

#include "semitrivial/csv.h"

#define MAX_LINE_SIZE 65536  // Increased from 8192 for better performance
#define BUFFER_SIZE (2 * 1024 * 1024)  // 2MB buffer for file reading

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// Print usage information
void print_usage(const char* program_name) {
    fprintf(stderr, "Usage: %s [options] <csvfile>\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -s, --stream       Use streaming parser (default, only mode available)\n");
    fprintf(stderr, "  -m, --mmap         Use memory-mapped parser (not available)\n");
    fprintf(stderr, "  -p, --parallel     Enable multi-threaded parsing (not available)\n");
    fprintf(stderr, "  -S, --single       Use single-threaded parsing (default)\n");
    fprintf(stderr, "  -b, --buffer=SIZE  Set buffer size in KB (default: 2048)\n");
    fprintf(stderr, "  -v, --verbose      Show detailed progress\n");
    fprintf(stderr, "  -h, --help         Show this help message\n");
}

int main(int argc, char** argv) {
    // Default options
    bool use_mmap = false;  // Alexander doesn't support memory mapping
    bool use_threads = false;  // Alexander doesn't support threading
    bool verbose = false;
    size_t buffer_size = BUFFER_SIZE;
    
    // Command line options
    static struct option long_options[] = {
        {"stream",   no_argument,       0, 's'},
        {"mmap",     no_argument,       0, 'm'},
        {"parallel", no_argument,       0, 'p'},
        {"single",   no_argument,       0, 'S'},
        {"buffer",   required_argument, 0, 'b'},
        {"verbose",  no_argument,       0, 'v'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "smpSb:vh", long_options, &option_index)) != -1) {
        switch (opt) {
            case 's':
                use_mmap = false;
                break;
            case 'm':
                fprintf(stderr, "Warning: Alexander does not support memory-mapped parsing\n");
                use_mmap = false;  // Ignore request and fall back to streaming
                break;
            case 'p':
                fprintf(stderr, "Warning: Alexander does not support multi-threaded parsing\n");
                use_threads = false;  // Ignore request and fall back to single-threaded
                break;
            case 'S':
                use_threads = false;
                break;
            case 'b':
                buffer_size = atoi(optarg) * 1024;
                if (buffer_size < 4096) {
                    fprintf(stderr, "Warning: Buffer size too small, using 4KB\n");
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
        fprintf(stderr, "Error: Missing input CSV file\n");
        print_usage(argv[0]);
        return 1;
    }

    const char* filename = argv[optind];
    
    // Get file size for throughput calculation
    struct stat st;
    size_t file_size = 0;
    if (stat(filename, &st) == 0) {
        file_size = (size_t)st.st_size;
    } else {
        fprintf(stderr, "Error: Cannot determine file size for %s\n", filename);
        return 1;
    }
    
    if (file_size == 0) {
        fprintf(stderr, "Error: File is empty: %s\n", filename);
        return 1;
    }
    
    // Display parsing mode
    printf("Parsing mode: %s, Threading: %s, Buffer: %.1f KB\n", 
           use_mmap ? "Memory-mapped" : "Streaming",
           use_threads ? "Multi-threaded" : "Single-threaded",
           buffer_size / 1024.0);
    printf("File: %s (%.2f MB)\n", filename, file_size / (1024.0 * 1024.0));

    FILE *fp = fopen(filename, "rb");  // Use binary mode for better performance
    if (!fp) {
        perror("fopen");
        return 1;
    }

    // Set larger buffer for file operations
    if (setvbuf(fp, NULL, _IOFBF, buffer_size) != 0) {
        fprintf(stderr, "Warning: Failed to set file buffer size\n");
    }

    int done = 0, err = 0;
    uint64_t lines = 0, fields = 0;
    double t0 = now_sec();
    
    // Progress tracking
    size_t progress_step = 100000;
    if (file_size > 100 * 1024 * 1024) { // > 100MB
        progress_step = 1000000; // Update every 1M lines for large files
    }

    while (!done) {
        char *line = fread_csv_line(fp, MAX_LINE_SIZE, &done, &err);
        if (!line) {
            if (err != 0) {
                fprintf(stderr, "Error reading line %lu: %d\n", lines + 1, err);
            }
            continue;
        }

        char **parsed = parse_csv(line);
        if (parsed) {
            // Count fields efficiently
            int field_count = 0;
            for (int i = 0; parsed[i]; i++) {
                field_count++;
            }
            fields += field_count;
            free_csv_line(parsed);
        }
        free(line);
        lines++;
        
        // Progress updates for verbose mode
        if (verbose && lines % progress_step == 0) {
            double progress_percent = 0.0;
            if (file_size > 0) {
                size_t current_pos = ftell(fp);
                progress_percent = (double)current_pos / file_size * 100.0;
            }
            fprintf(stderr, "\rProcessing: %lu lines (%.1f%%)", lines, progress_percent);
            fflush(stderr);
        }
    }

    double t1 = now_sec();
    double elapsed = t1 - t0;
    
    // Clear progress line if verbose
    if (verbose) {
        fprintf(stderr, "\r                                        \r");
    }
    
    // Calculate throughput
    double throughput_mbps = 0.0;
    if (elapsed > 0) {
        throughput_mbps = (file_size / (1024.0 * 1024.0)) / elapsed;
    }
    
    printf("Parsed %lu lines (%lu fields) in %.6f seconds (%.2f lines/sec)\n",
           lines, fields, elapsed, lines / elapsed);
    printf("Throughput: %.2f MB/s\n", throughput_mbps);
    printf("Average fields per line: %.1f\n", 
           lines > 0 ? (double)fields / lines : 0.0);

    fclose(fp);
    return 0;
}