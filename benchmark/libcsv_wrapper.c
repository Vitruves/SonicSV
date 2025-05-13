/**
 * SonicSV Benchmark Suite
 * 
 * Performance comparison between SonicSV and other CSV parsing libraries.
 * 
 * This file provides a wrapper for libcsv (https://github.com/rgamble/libcsv)
 * by Robert Gamble, which is used for benchmarking comparisons.
 * 
 * libcsv is licensed under LGPL-2.1 and available at:
 * https://github.com/rgamble/libcsv
 * 
 * GitHub: https://github.com/Vitruves/SonicSV
 * 
 * Copyright (c) 2025 SonicSV Contributors
 * MIT License
 */

#include "libcsv_wrapper.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <csv.h>

#define LIBCSV_ERROR_BUFFER_SIZE 256
#define LIBCSV_INITIAL_BUFFER_SIZE 4096
#define LIBCSV_MAX_FIELDS 1024
#define LIBCSV_DEFAULT_CAPACITY 16

// Error message buffer
static char error_buffer[LIBCSV_ERROR_BUFFER_SIZE];

// CSV Row structure
struct LibCSVRow {
    char** fields;
    size_t field_count;
    size_t capacity;
};

// CSV structure
struct LibCSVWrapper {
    LibCSVRow** rows;
    size_t row_count;
    size_t capacity;
    char delimiter;
};

// Parser state
typedef struct {
    LibCSVWrapper* csv;
    LibCSVRow* current_row;
} LibCSVParserState;

// Helper functions
static void set_error(const char* format, ...);
static LibCSVWrapper* libcsv_create(char delimiter);
static LibCSVRow* libcsv_row_create(void);
static bool libcsv_ensure_capacity(LibCSVWrapper* csv, size_t capacity);
static bool libcsv_row_ensure_capacity(LibCSVRow* row, size_t capacity);
static void field_callback(void* data, size_t len, void* state_ptr);
static void row_callback(int c, void* state_ptr);
static void libcsv_row_free_internal(LibCSVRow* row);

// Set error message
static void set_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(error_buffer, LIBCSV_ERROR_BUFFER_SIZE, format, args);
    va_end(args);
}

// Create a new CSV with default capacity
static LibCSVWrapper* libcsv_create(char delimiter) {
    LibCSVWrapper* csv = (LibCSVWrapper*)malloc(sizeof(LibCSVWrapper));
    if (!csv) {
        set_error("Memory allocation failed");
        return NULL;
    }
    
    csv->rows = (LibCSVRow**)malloc(sizeof(LibCSVRow*) * LIBCSV_DEFAULT_CAPACITY);
    if (!csv->rows) {
        free(csv);
        set_error("Memory allocation failed");
        return NULL;
    }
    
    csv->row_count = 0;
    csv->capacity = LIBCSV_DEFAULT_CAPACITY;
    csv->delimiter = delimiter;
    
    return csv;
}

// Create a new CSV row with default capacity
static LibCSVRow* libcsv_row_create(void) {
    LibCSVRow* row = (LibCSVRow*)malloc(sizeof(LibCSVRow));
    if (!row) {
        set_error("Memory allocation failed for row struct");
        return NULL;
    }
    
    row->fields = (char**)malloc(sizeof(char*) * LIBCSV_DEFAULT_CAPACITY);
    if (!row->fields) {
        free(row);
        set_error("Memory allocation failed for row fields");
        return NULL;
    }
    
    row->field_count = 0;
    row->capacity = LIBCSV_DEFAULT_CAPACITY;
    
    return row;
}

// Ensure CSV has enough capacity
static bool libcsv_ensure_capacity(LibCSVWrapper* csv, size_t capacity) {
    if (capacity <= csv->capacity) {
        return true;
    }
    
    size_t new_capacity = csv->capacity * 2;
    while (new_capacity < capacity) {
        new_capacity *= 2;
    }
    
    LibCSVRow** new_rows = (LibCSVRow**)realloc(csv->rows, sizeof(LibCSVRow*) * new_capacity);
    if (!new_rows) {
        set_error("Memory allocation failed");
        return false;
    }
    
    csv->rows = new_rows;
    csv->capacity = new_capacity;
    
    return true;
}

// Ensure CSV row has enough capacity
static bool libcsv_row_ensure_capacity(LibCSVRow* row, size_t capacity) {
    if (capacity <= row->capacity) {
        return true;
    }
    
    size_t new_capacity = row->capacity * 2;
    while (new_capacity < capacity) {
        new_capacity *= 2;
    }
    
    char** new_fields = (char**)realloc(row->fields, sizeof(char*) * new_capacity);
    if (!new_fields) {
        set_error("Memory allocation failed");
        return false;
    }
    
    row->fields = new_fields;
    row->capacity = new_capacity;
    
    return true;
}

// Added implementation for freeing a single row
static void libcsv_row_free_internal(LibCSVRow* row) {
    if (!row) return;
    for (size_t j = 0; j < row->field_count; j++) {
        free(row->fields[j]);
    }
    free(row->fields);
    free(row);
}

// Callback for field parsing
static void field_callback(void* data, size_t len, void* state_ptr) {
    LibCSVParserState* state = (LibCSVParserState*)state_ptr;
    
    // Ensure current_row exists (should be created by row_callback or initial setup)
    if (!state->current_row) {
        // This might indicate an issue in row_callback logic or initial state
        set_error("Parser state error: current_row is NULL in field_callback");
        return;
    }
    
    // Make a copy of the field data
    char* field_data = malloc(len + 1);
    if (!field_data) {
        set_error("Memory allocation failed for field data");
        // Potentially mark an error state in LibCSVParserState if needed
        return;
    }
    
    memcpy(field_data, data, len);
    field_data[len] = '\0';
    
    // Add field to current row
    if (libcsv_row_ensure_capacity(state->current_row, state->current_row->field_count + 1)) {
        state->current_row->fields[state->current_row->field_count++] = field_data;
    } else {
        // Failed to expand row capacity, free the copied field
        free(field_data);
        // Error message is set by libcsv_row_ensure_capacity
        // Consider how to signal this failure back up if necessary
    }
}

// Callback for row completion
static void row_callback(int c, void* state_ptr) {
    LibCSVParserState* state = (LibCSVParserState*)state_ptr;
    LibCSVWrapper* csv = state->csv;
    
    // Finalize the completed row (if one exists)
    if (state->current_row) {
        // Add the completed row to the main CSV structure
        if (libcsv_ensure_capacity(csv, csv->row_count + 1)) {
            csv->rows[csv->row_count++] = state->current_row;
        } else {
            // Failed to add row to CSV, free the row we just parsed
            libcsv_row_free_internal(state->current_row); // Use the internal helper
            // Error message is set by libcsv_ensure_capacity
        }
    }
    
    // Start a new row for the next set of fields (unless it's the final call)
    // libcsv calls row_callback with c = -1 at the very end after all data.
    if (c != -1) {
        state->current_row = libcsv_row_create();
        if (!state->current_row) {
            // Critical error, cannot create next row.
            // Consider how to handle this - maybe stop parsing?
            set_error("Failed to create new row during parsing");
            // The parser might continue, leading to issues in field_callback.
            // A more robust solution might involve setting a flag in 'state'.
        }
    } else {
        // This was the final call, no need to create a new row.
        state->current_row = NULL;
    }
}

// Read CSV from file
LibCSVWrapper* libcsv_read_file(const char* filename) {
    return libcsv_read_file_with_delimiter(filename, ',');
}

// Read CSV from string
LibCSVWrapper* libcsv_read_string(const char* data, size_t length) {
    return libcsv_read_string_with_delimiter(data, length, ',');
}

// Read CSV from file with custom delimiter
LibCSVWrapper* libcsv_read_file_with_delimiter(const char* filename, char delimiter) {
    if (!filename) {
        set_error("Filename cannot be NULL");
        return NULL;
    }
    
    FILE* file = fopen(filename, "r");
    if (!file) {
        set_error("Failed to open file: %s", filename);
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    
    // Read entire file into memory
    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        fclose(file);
        set_error("Memory allocation failed");
        return NULL;
    }
    
    size_t read_size = fread(buffer, 1, size, file);
    fclose(file);
    
    if (read_size != (size_t)size) {
        free(buffer);
        set_error("Failed to read file");
        return NULL;
    }
    
    buffer[size] = '\0';
    
    // Parse the CSV data
    LibCSVWrapper* csv = libcsv_read_string_with_delimiter(buffer, size, delimiter);
    
    free(buffer);
    return csv;
}

// Read CSV from string with custom delimiter
LibCSVWrapper* libcsv_read_string_with_delimiter(const char* data, size_t length, char delimiter) {
    if (!data) {
        set_error("Data cannot be NULL");
        return NULL;
    }
    
    // Create CSV wrapper
    LibCSVWrapper* csv = libcsv_create(delimiter);
    if (!csv) {
        return NULL;
    }
    
    // Initialize parser state
    LibCSVParserState state;
    state.csv = csv;
    state.current_row = libcsv_row_create(); // Create the first row initially
    
    if (!state.current_row) {
        libcsv_free(csv); // Free the wrapper if row creation failed
        return NULL;
    }
    
    // Initialize libcsv parser
    struct csv_parser parser;
    // Consider adding CSV_APPEND_NULL to automatically null-terminate fields if desired
    if (csv_init(&parser, CSV_STRICT | CSV_STRICT_FINI) != 0) {
        set_error("Failed to initialize libcsv parser");
        libcsv_row_free_internal(state.current_row); // Free the initial row
        libcsv_free(csv);
        return NULL;
    }
    
    // Set delimiter
    csv_set_delim(&parser, delimiter);
    
    // Parse data
    if (csv_parse(&parser, data, length, field_callback, row_callback, &state) != length) {
        set_error("CSV parsing error: %s", csv_strerror(csv_error(&parser)));
        // state.current_row might hold a partially parsed row here.
        libcsv_row_free_internal(state.current_row);
        csv_free(&parser);
        libcsv_free(csv);
        return NULL;
    }
    
    // Finalize parsing (calls row_callback one last time with c = -1)
    csv_fini(&parser, field_callback, row_callback, &state);
    
    // Clean up parser itself
    csv_free(&parser);
    
    // Check if the very last row (processed in csv_fini) failed to be added
    // This check might be redundant if errors are handled well within callbacks
    if(state.current_row != NULL) {
        // This indicates an error, likely memory allocation failure during the final row_callback
        // Free the dangling row.
        libcsv_row_free_internal(state.current_row);
        // The main csv structure might be incomplete. Decide whether to return NULL or the partial result.
        // For now, return the potentially partial result as errors were set earlier.
    }
    
    return csv;
}

// Write CSV to file
bool libcsv_write_file(LibCSVWrapper* csv, const char* filename) {
    if (!csv || !filename) {
        set_error("CSV or filename cannot be NULL");
        return false;
    }
    
    FILE* file = fopen(filename, "w");
    if (!file) {
        set_error("Failed to open file for writing: %s", filename);
        return false;
    }
    
    bool success = true;
    
    for (size_t i = 0; i < csv->row_count; i++) {
        LibCSVRow* row = csv->rows[i];
        
        for (size_t j = 0; j < row->field_count; j++) {
            // Write field
            const char* field = row->fields[j];
            bool needs_quotes = false;
            
            // Check if field needs quoting
            for (size_t k = 0; field[k]; k++) {
                if (field[k] == csv->delimiter || field[k] == '"' || 
                    field[k] == '\n' || field[k] == '\r') {
                    needs_quotes = true;
                    break;
                }
            }
            
            if (needs_quotes) {
                fputc('"', file);
                for (size_t k = 0; field[k]; k++) {
                    if (field[k] == '"') {
                        fputc('"', file); // Escape quotes with another quote
                    }
                    fputc(field[k], file);
                }
                fputc('"', file);
            } else {
                fputs(field, file);
            }
            
            // Add delimiter between fields
            if (j < row->field_count - 1) {
                fputc(csv->delimiter, file);
            }
        }
        
        // Add newline after each row
        if (i < csv->row_count - 1) {
            fputc('\n', file);
        }
    }
    
    fclose(file);
    return success;
}

// Helper function to ensure buffer has enough space - MOVED HERE
static bool ensure_buffer_capacity_for_write(char** buffer, size_t* buffer_size, size_t buffer_pos, size_t needed) {
    if (buffer_pos + needed >= *buffer_size) {
        size_t new_size = (*buffer_size == 0) ? LIBCSV_INITIAL_BUFFER_SIZE : *buffer_size * 2;
        // Ensure the new size is definitely large enough
        while (new_size < buffer_pos + needed) {
            new_size *= 2;
             // Add a safety break for runaway allocations
             if (new_size > 1024*1024*512) { // 512MB limit? Adjust as needed.
                 set_error("Write buffer allocation exceeded safety limit");
                 return false;
             }
        }

        char* new_buffer = (char*)realloc(*buffer, new_size);
        if (!new_buffer) {
            set_error("Memory allocation failed during write buffer resize");
            return false;
        }
        *buffer = new_buffer;
        *buffer_size = new_size;
    }
    return true;
}

// Write CSV to string
char* libcsv_write_string(LibCSVWrapper* csv, size_t* length) {
    if (!csv) {
        set_error("CSV cannot be NULL");
        return NULL;
    }
    
    // Estimate buffer size needed
    size_t buffer_size = 0; // Start at 0, let helper allocate
    size_t buffer_pos = 0;
    char* buffer = NULL; // Start as NULL
    
    // Write rows and fields
    for (size_t i = 0; i < csv->row_count; i++) {
        LibCSVRow* row = csv->rows[i];
        
        for (size_t j = 0; j < row->field_count; j++) {
            const char* field = row->fields[j];
             if (!field) field = ""; // Handle NULL fields
            size_t field_len = strlen(field);
            bool needs_quotes = false;
            size_t quote_count = 0;

            // Check if field needs quoting and count quotes
            for (size_t k = 0; k < field_len; k++) {
                if (field[k] == csv->delimiter || field[k] == '"' ||
                    field[k] == '\n' || field[k] == '\r') {
                    needs_quotes = true;
                }
                 if (field[k] == '"') quote_count++;
            }
             // Calculate space needed for this field + potential delimiter/newline
             size_t needed_field_space = field_len + quote_count + (needs_quotes ? 2 : 0);
             size_t needed_extra_space = 1; // For delimiter or newline


            if (!ensure_buffer_capacity_for_write(&buffer, &buffer_size, buffer_pos, needed_field_space + needed_extra_space)) {
                 free(buffer); // Free partially built buffer
                 return NULL;
             }


            if (needs_quotes) {
                buffer[buffer_pos++] = '"';
                for (size_t k = 0; k < field_len; k++) {
                    if (field[k] == '"') {
                        buffer[buffer_pos++] = '"'; // Escape quote
                    }
                    buffer[buffer_pos++] = field[k];
                }
                buffer[buffer_pos++] = '"';
            } else {
                memcpy(buffer + buffer_pos, field, field_len);
                buffer_pos += field_len;
            }

            // Add delimiter between fields
            if (j < row->field_count - 1) {
                 buffer[buffer_pos++] = csv->delimiter;
            }
        }

        // Add newline after each row (except the last)
        // No need to check capacity here because we allocated extra space above
        if (i < csv->row_count - 1) {
             buffer[buffer_pos++] = '\n';
        }
    }

    // Null-terminate the string
    if (!ensure_buffer_capacity_for_write(&buffer, &buffer_size, buffer_pos, 1)) {
        free(buffer);
        return NULL;
    }
    buffer[buffer_pos] = '\0';

    // Set length if requested
    if (length) {
        *length = buffer_pos;
    }

     // Optional: Shrink buffer to exact size (might save memory but adds overhead)
     // char* final_buffer = realloc(buffer, buffer_pos + 1);
     // if (final_buffer) {
     //     buffer = final_buffer;
     // } // else use the slightly larger buffer

    return buffer;
}

// Get row count
size_t libcsv_get_row_count(const LibCSVWrapper* csv) {
    return csv ? csv->row_count : 0;
}

// Get column count for a row
size_t libcsv_get_column_count(const LibCSVWrapper* csv, size_t row_index) {
    if (!csv || row_index >= csv->row_count) {
        return 0;
    }
    
    return csv->rows[row_index]->field_count;
}

// Get field value
const char* libcsv_get_field(const LibCSVWrapper* csv, size_t row_index, size_t column_index) {
    if (!csv || row_index >= csv->row_count || 
        column_index >= csv->rows[row_index]->field_count) {
        return NULL;
    }
    
    return csv->rows[row_index]->fields[column_index];
}

// Get field count for a row (alias for get_column_count for API compatibility)
size_t libcsv_get_field_count(const LibCSVWrapper* csv, size_t row_index) {
    return libcsv_get_column_count(csv, row_index);
}

// Free CSV memory
void libcsv_free(LibCSVWrapper* csv) {
    if (!csv) return;
    
    for (size_t i = 0; i < csv->row_count; i++) {
        // Use the internal row free function
        libcsv_row_free_internal(csv->rows[i]);
    }
    
    free(csv->rows);
    free(csv);
}

// Get last error message
const char* libcsv_get_error(void) {
    return error_buffer;
}