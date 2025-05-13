/**
 * SonicSV - Advanced CSV Parsing Example
 * 
 * This example demonstrates advanced features of SonicSV including:
 * - Custom configuration
 * - Header detection
 * - Field type conversion
 * - Filtering rows
 * 
 * GitHub: https://github.com/Vitruves/SonicSV
 * 
 * Copyright (c) 2025 SonicSV Contributors
 * MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <strings.h> /* For strcasecmp */
#include "../sonicsv.h"

/* For strdup on some systems that don't include it in string.h */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ < 201112L
  char* strdup(const char* s) {
    size_t len = strlen(s) + 1;
    char* new = malloc(len);
    if (new) {
        memcpy(new, s, len);
    }
    return new;
  }
#endif

// Max number of columns to track
#define MAX_COLUMNS 100

// Define column types
typedef enum {
    TYPE_UNKNOWN,
    TYPE_STRING,
    TYPE_INTEGER,
    TYPE_FLOAT,
    TYPE_BOOLEAN,
    TYPE_EMPTY
} ColumnType;

// Column metadata
typedef struct {
    char* name;
    ColumnType type;
    int index;
} ColumnInfo;

// Helper function to case insensitive string comparison (fallback if strcasecmp not available)
static int str_case_cmp(const char* s1, const char* s2) {
#if defined(_WIN32) || defined(_MSC_VER)
    return _stricmp(s1, s2);
#else
    return strcasecmp(s1, s2);
#endif
}

// Helper function to guess column type from a value
static ColumnType guess_type(const char* value) {
    if (!value || value[0] == '\0') {
        return TYPE_EMPTY;
    }
    
    // Check if it's a boolean
    if (str_case_cmp(value, "true") == 0 || str_case_cmp(value, "false") == 0 ||
        str_case_cmp(value, "yes") == 0 || str_case_cmp(value, "no") == 0 ||
        str_case_cmp(value, "1") == 0 || str_case_cmp(value, "0") == 0) {
        return TYPE_BOOLEAN;
    }
    
    // Check if it's an integer
    bool is_integer = true;
    int i = 0;
    
    // Skip leading sign
    if (value[i] == '-' || value[i] == '+') {
        i++;
    }
    
    // Must have at least one digit
    if (!isdigit((unsigned char)value[i])) {
        is_integer = false;
    }
    
    // Check remaining characters
    for (; value[i]; i++) {
        if (!isdigit((unsigned char)value[i])) {
            is_integer = false;
            break;
        }
    }
    
    if (is_integer) {
        return TYPE_INTEGER;
    }
    
    // Check if it's a float
    bool is_float = true;
    i = 0;
    bool has_decimal = false;
    
    // Skip leading sign
    if (value[i] == '-' || value[i] == '+') {
        i++;
    }
    
    // Process digits and decimal point
    for (; value[i]; i++) {
        if (value[i] == '.') {
            if (has_decimal) {
                is_float = false;
                break;
            }
            has_decimal = true;
        } else if (!isdigit((unsigned char)value[i])) {
            is_float = false;
            break;
        }
    }
    
    if (is_float && has_decimal) {
        return TYPE_FLOAT;
    }
    
    // Default to string
    return TYPE_STRING;
}

// Helper function to get type name
static const char* type_name(ColumnType type) {
    switch (type) {
        case TYPE_INTEGER: return "Integer";
        case TYPE_FLOAT:   return "Float";
        case TYPE_BOOLEAN: return "Boolean";
        case TYPE_STRING:  return "String";
        case TYPE_EMPTY:   return "Empty";
        default:           return "Unknown";
    }
}

// Convert field to its inferred type
static void process_field(const char* value, ColumnType type) {
    switch (type) {
        case TYPE_INTEGER:
            printf("%ld", strtol(value, NULL, 10));
            break;
        case TYPE_FLOAT:
            printf("%f", strtod(value, NULL));
            break;
        case TYPE_BOOLEAN:
            if (str_case_cmp(value, "true") == 0 || str_case_cmp(value, "yes") == 0 || 
                str_case_cmp(value, "1") == 0) {
                printf("true");
            } else {
                printf("false");
            }
            break;
        case TYPE_STRING:
            printf("\"%s\"", value);
            break;
        case TYPE_EMPTY:
            printf("null");
            break;
        default:
            printf("\"%s\"", value);
            break;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <csv_file> [delimiter]\n", argv[0]);
        return 1;
    }
    
    const char* filename = argv[1];
    
    // Create custom configuration
    SonicSVConfig config = sonicsv_default_config();
    
    // Set custom delimiter if provided
    if (argc > 2 && argv[2][0]) {
        config.delimiter = argv[2][0];
    }
    
    printf("-- SonicSV Advanced Parsing Example --\n");
    printf("Processing file: %s\n", filename);
    printf("Delimiter: '%c'\n\n", config.delimiter);
    
    // Open CSV file
    SonicSV* stream = sonicsv_open(filename, &config);
    if (!stream) {
        printf("Error: %s\n", sonicsv_get_error());
        return 1;
    }
    
    // Read header row
    SonicSVRow* header_row = sonicsv_next_row(stream);
    if (!header_row) {
        printf("Error: Empty file or error reading header\n");
        sonicsv_close(stream);
        return 1;
    }
    
    // Initialize column information
    size_t num_columns = sonicsv_row_get_field_count(header_row);
    if (num_columns > MAX_COLUMNS) {
        num_columns = MAX_COLUMNS;
    }
    
    ColumnInfo columns[MAX_COLUMNS];
    for (size_t i = 0; i < num_columns; i++) {
        const char* field_name = sonicsv_row_get_field(header_row, i);
        columns[i].name = strdup(field_name ? field_name : "");
        columns[i].type = TYPE_UNKNOWN;
        columns[i].index = i;
    }
    
    printf("Detected %zu columns:\n", num_columns);
    for (size_t i = 0; i < num_columns; i++) {
        printf("  %zu: %s\n", i, columns[i].name);
    }
    printf("\n");
    
    // Process and sample data rows to infer types
    SonicSVRow* row;
    size_t row_count = 0;
    size_t sample_size = 100;  // Number of rows to sample for type inference
    
    printf("Sampling %zu rows to infer column types...\n", sample_size);
    
    while (row_count < sample_size && (row = sonicsv_next_row(stream)) != NULL) {
        row_count++;
        
        for (size_t i = 0; i < num_columns; i++) {
            const char* value = sonicsv_row_get_field(row, i);
            
            // Only infer type if not already determined or currently unknown
            if (columns[i].type == TYPE_UNKNOWN || columns[i].type == TYPE_EMPTY) {
                columns[i].type = guess_type(value);
            } 
            // If type conflicts, fall back to string
            else if (columns[i].type != TYPE_STRING) {
                ColumnType new_type = guess_type(value);
                if (new_type != columns[i].type && new_type != TYPE_EMPTY) {
                    // Type conflict detected, fall back to string
                    columns[i].type = TYPE_STRING;
                }
            }
        }
        
        sonicsv_row_free(row);
    }
    
    // Display inferred column types
    printf("\nInferred column types:\n");
    for (size_t i = 0; i < num_columns; i++) {
        printf("  %s: %s\n", columns[i].name, type_name(columns[i].type));
    }
    
    // Reset file for full processing
    sonicsv_close(stream);
    stream = sonicsv_open(filename, &config);
    if (!stream) {
        printf("Error reopening file: %s\n", sonicsv_get_error());
        return 1;
    }
    
    // Skip header row
    row = sonicsv_next_row(stream);
    sonicsv_row_free(row);
    
    // Process all rows and convert fields to their inferred types
    printf("\nProcessing data with inferred types (first 5 rows):\n");
    row_count = 0;
    
    while ((row = sonicsv_next_row(stream)) != NULL) {
        row_count++;
        
        // Only show the first 5 rows
        if (row_count <= 5) {
            for (size_t i = 0; i < num_columns; i++) {
                if (i > 0) printf(", ");
                
                const char* value = sonicsv_row_get_field(row, i);
                process_field(value ? value : "", columns[i].type);
            }
            printf("\n");
        }
        
        sonicsv_row_free(row);
    }
    
    // Clean up
    sonicsv_close(stream);
    for (size_t i = 0; i < num_columns; i++) {
        free(columns[i].name);
    }
    
    printf("\nProcessed %zu data rows successfully.\n", row_count);
    return 0;
}