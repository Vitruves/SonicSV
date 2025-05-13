/**
 * SonicSV - Simple CSV Parsing Example
 * 
 * This example demonstrates basic CSV file parsing with SonicSV.
 * 
 * GitHub: https://github.com/Vitruves/SonicSV
 * 
 * Copyright (c) 2025 SonicSV Contributors
 * MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sonicsv.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <csv_file>\n", argv[0]);
        return 1;
    }
    
    const char* filename = argv[1];
    printf("-- SonicSV Simple Parsing Example --\n");
    printf("Processing file: %s\n\n", filename);
    
    // Get default configuration
    SonicSVConfig config = sonicsv_default_config();
    
    // Open CSV file
    SonicSV* stream = sonicsv_open(filename, &config);
    if (!stream) {
        printf("Error: %s\n", sonicsv_get_error());
        return 1;
    }
    
    // Process each row
    size_t row_count = 0;
    SonicSVRow* row;
    
    printf("First 5 rows of the CSV file:\n");
    printf("-----------------------------\n");
    
    while ((row = sonicsv_next_row(stream)) != NULL) {
        row_count++;
        
        // Print only the first 5 rows
        if (row_count <= 5) {
            size_t fields = sonicsv_row_get_field_count(row);
            
            // Print field values
            for (size_t i = 0; i < fields; i++) {
                if (i > 0) printf(", ");
                const char* value = sonicsv_row_get_field(row, i);
                printf("\"%s\"", value ? value : "");
            }
            printf("\n");
        }
        
        // Display progress for large files
        if (row_count % 1000000 == 0) {
            printf("Processed %zu million rows...\n", row_count / 1000000);
        }
        
        // Free row memory
        sonicsv_row_free(row);
    }
    
    // Close stream
    sonicsv_close(stream);
    
    printf("\nTotal rows processed: %zu\n", row_count);
    printf("Done!\n");
    
    return 0;
}