/*
 * SonicSV Usage Example
 * 
 * This demonstrates basic usage of the SonicSV high-performance CSV parser.
 * Compile with: gcc -std=c11 -O2 -o example example.c -lm -lpthread
 */

#include "../sonicsv.h"

#include <stdio.h>
#include <stdlib.h>

// Callback function to process each parsed row
void row_callback(const csv_row_t *row, void *user_data) {
    int *row_count = (int*)user_data;
    (*row_count)++;
    
    printf("Row %d: ", *row_count);
    for (size_t i = 0; i < row->num_fields; i++) {
        const csv_field_t *field = csv_get_field(row, i);
        if (i > 0) printf(", ");
        printf("'%.*s'", (int)field->size, field->data);
    }
    printf("\n");
}

// Error callback to handle parsing errors
void error_callback(csv_error_t error, const char *message, 
                   uint64_t row_number, void *user_data) {
    (void)user_data;  // Suppress unused parameter warning
    fprintf(stderr, "Error on row %llu: %s (%s)\n", 
            (unsigned long long)row_number, message, csv_error_string(error));
}

int main() {
    printf("SonicSV Example - High-Performance CSV Parser\n");
    printf("=============================================\n\n");
    
    // Create parser with default options
    csv_parser_t *parser = csv_parser_create(NULL);
    if (!parser) {
        fprintf(stderr, "Failed to create parser\n");
        return 1;
    }
    
    // Set up callbacks
    int row_count = 0;
    csv_parser_set_row_callback(parser, row_callback, &row_count);
    csv_parser_set_error_callback(parser, error_callback, NULL);
    
    // Example 1: Parse a simple CSV string
    printf("Example 1: Basic CSV parsing\n");
    const char *csv_data = "name,age,city\nJohn,25,New York\nJane,30,Boston\n";
    csv_error_t result = csv_parse_string(parser, csv_data);
    
    if (result != CSV_OK) {
        fprintf(stderr, "Parse error: %s\n", csv_error_string(result));
        csv_parser_destroy(parser);
        return 1;
    }
    
    printf("\n");
    
    // Reset parser for next example
    csv_parser_reset(parser);
    row_count = 0;
    
    // Example 2: Parse CSV with custom options
    printf("Example 2: Custom delimiter and whitespace trimming\n");
    csv_parse_options_t opts = csv_default_options();
    opts.delimiter = ';';              // Use semicolon as delimiter
    opts.trim_whitespace = true;       // Trim whitespace from fields
    
    csv_parser_destroy(parser);  // Destroy old parser
    parser = csv_parser_create(&opts);
    csv_parser_set_row_callback(parser, row_callback, &row_count);
    
    const char *csv_data2 = "name ; age ; city\n John ; 25 ; New York \n Jane ; 30 ; Boston ";
    result = csv_parse_string(parser, csv_data2);
    
    if (result != CSV_OK) {
        fprintf(stderr, "Parse error: %s\n", csv_error_string(result));
        csv_parser_destroy(parser);
        return 1;
    }
    
    printf("\n");
    
    // Example 3: Show performance statistics
    printf("Example 3: Performance statistics\n");
    csv_print_stats(parser);
    
    printf("\nExample 4: Available SIMD features\n");
    uint32_t simd_features = csv_get_simd_features();
    printf("SIMD Features: ");
    if (simd_features == CSV_SIMD_NONE) {
        printf("None (scalar fallback)");
    } else {
        if (simd_features & CSV_SIMD_AVX512) printf("AVX-512 ");
        if (simd_features & CSV_SIMD_AVX2) printf("AVX2 ");
        if (simd_features & CSV_SIMD_SSE4_2) printf("SSE4.2 ");
        if (simd_features & CSV_SIMD_NEON) printf("NEON ");
        if (simd_features & CSV_SIMD_SVE) printf("SVE ");
    }
    printf("\n");
    
    // Clean up
    csv_parser_destroy(parser);
    
    printf("\nExample completed successfully!\n");
    return 0;
}