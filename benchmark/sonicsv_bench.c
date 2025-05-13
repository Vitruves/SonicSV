#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>

// Include SonicSV and libcsv libraries for comparison
#include "../sonicsv.h"
#include "libcsv_wrapper.h"

// Function to get high-precision time
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

// Benchmark function for SonicSV
static void benchmark_sonicsv(const char* filename) {
    printf("\n-- SonicSV Benchmark:\n");
    double start_time = get_time_ms();
    
    // Create configuration with defaults
    SonicSVConfig config = sonicsv_default_config();
    
    // Open CSV file
    SonicSV* stream = sonicsv_open(filename, &config);
    if (!stream) {
        printf("Error: %s\n", sonicsv_get_error());
        return;
    }
    
    // Process CSV rows
    size_t total_rows = 0;
    size_t total_fields = 0;
    SonicSVRow* row;
    
    while ((row = sonicsv_next_row(stream)) != NULL) {
        total_rows++;
        total_fields += sonicsv_row_get_field_count(row);
        
        // Process first 3 rows for display
        if (total_rows <= 3) {
            printf("Row %zu: ", total_rows);
            for (size_t i = 0; i < sonicsv_row_get_field_count(row) && i < 3; i++) {
                if (i > 0) printf(", ");
                printf("\"%s\"", sonicsv_row_get_field(row, i));
            }
            if (sonicsv_row_get_field_count(row) > 3) {
                printf(", ... (%zu more fields)", sonicsv_row_get_field_count(row) - 3);
            }
            printf("\n");
        }
        
        // Print progress
        if (total_rows % 250000 == 0) {
            printf("  Processed %zu rows...\n", total_rows);
        }
        
        sonicsv_row_free(row);
    }
    
    // Close the stream
    sonicsv_close(stream);
    
    // Calculate and display results
    double end_time = get_time_ms();
    double time_taken = (end_time - start_time) / 1000.0;
    
    printf("SonicSV Results:\n");
    printf("  Total rows: %zu\n", total_rows);
    printf("  Total fields: %zu\n", total_fields);
    printf("  Time: %.2f seconds\n", time_taken);
    if (time_taken > 0) {
        printf("  Speed: %.2f rows/second\n", total_rows / time_taken);
        printf("  Avg fields per row: %.2f\n", (double)total_fields / total_rows);
    } else {
        printf("  Speed: N/A (time too small)\n");
        printf("  Avg fields per row: %.2f\n", total_rows > 0 ? (double)total_fields / total_rows : 0);
    }
}

// Benchmark function for libcsv
static void benchmark_libcsv(const char* filename) {
    printf("\n-- libcsv Benchmark:\n");
    double start_time = get_time_ms();
    
    // Open CSV file
    LibCSVWrapper* csv = libcsv_read_file(filename);
    if (!csv) {
        printf("Error: %s\n", libcsv_get_error());
        return;
    }
    
    // Process rows (already loaded by libcsv_read_file)
    size_t total_rows = libcsv_get_row_count(csv);
    size_t total_fields = 0;
    
    // Process each row
    for (size_t i = 0; i < total_rows; i++) {
        size_t fields = libcsv_get_field_count(csv, i);
        total_fields += fields;
        
        // Process first 3 rows for display
        if (i < 3) {
            printf("Row %zu: ", i + 1);
            for (size_t j = 0; j < fields && j < 3; j++) {
                if (j > 0) printf(", ");
                const char* field = libcsv_get_field(csv, i, j);
                printf("\"%s\"", field ? field : "NULL");
            }
            if (fields > 3) {
                printf(", ... (%zu more fields)", fields - 3);
            }
            printf("\n");
        }
        
        // Print progress
        if ((i + 1) % 250000 == 0) {
            printf("  Processed %zu rows...\n", i + 1);
        }
    }
    
    // Free the CSV
    libcsv_free(csv);
    
    // Calculate and display results
    double end_time = get_time_ms();
    double time_taken = (end_time - start_time) / 1000.0;
    
    printf("libcsv Results:\n");
    printf("  Total rows: %zu\n", total_rows);
    printf("  Total fields: %zu\n", total_fields);
    printf("  Time: %.2f seconds\n", time_taken);
    if (time_taken > 0) {
        printf("  Speed: %.2f rows/second\n", total_rows / time_taken);
        printf("  Avg fields per row: %.2f\n", (double)total_fields / total_rows);
    } else {
        printf("  Speed: N/A (time too small)\n");
        printf("  Avg fields per row: %.2f\n", total_rows > 0 ? (double)total_fields / total_rows : 0);
    }
}

// Print comparative results
static void print_comparison_table(const char* filename, double sonicsv_time, double libcsv_time) {
    printf("\n-- Comparative Performance Results --\n");
    printf("File: %s\n\n", filename);
    
    printf("| %-15s | %-15s | %-15s |\n", "Library", "Time (sec)", "Relative Speed");
    printf("|-----------------|-----------------|------------------|\n");
    
    // Find the fastest time for relative speed calculation
    double fastest_time = sonicsv_time < libcsv_time ? sonicsv_time : libcsv_time;
    
    // Print each library's performance
    printf("| %-15s | %15.2f | %15.2fx |\n", "SonicSV", sonicsv_time, fastest_time / sonicsv_time);
    printf("| %-15s | %15.2f | %15.2fx |\n", "libcsv", libcsv_time, fastest_time / libcsv_time);
    
    printf("\n");
}

/* Main benchmark function */
int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <csv_file>\n", argv[0]);
        return 1;
    }
    
    const char* filename = argv[1];
    printf("-- CSV Library Benchmark Suite --\n");
    printf("=================================\n");
    printf("Testing file: %s\n", filename);
    
    // Track times for comparison
    double start_time, end_time;
    double sonicsv_time = 0, libcsv_time = 0;
    
    // Benchmark SonicSV
    start_time = get_time_ms();
    benchmark_sonicsv(filename);
    end_time = get_time_ms();
    sonicsv_time = (end_time - start_time) / 1000.0;
    
    // Benchmark libcsv
    start_time = get_time_ms();
    benchmark_libcsv(filename);
    end_time = get_time_ms();
    libcsv_time = (end_time - start_time) / 1000.0;
    
    // Print comparison table
    print_comparison_table(filename, sonicsv_time, libcsv_time);
    
    return 0;
}