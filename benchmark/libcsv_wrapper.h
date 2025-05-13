/**
 * SonicSV Benchmark Suite
 * 
 * Performance comparison between SonicSV and other CSV parsing libraries.
 * 
 * This file provides a wrapper for libcsv (https://github.com/rgamble/libcsv)
 * by Robert Gamble, which is used for benchmarking comparisons.
 * 
 * GitHub: https://github.com/Vitruves/SonicSV
 * 
 * Copyright (c) 2025 SonicSV Contributors
 * MIT License
 */

#ifndef LIBCSV_WRAPPER_H
#define LIBCSV_WRAPPER_H

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <csv.h>

// Forward declarations
typedef struct LibCSVWrapper LibCSVWrapper;
typedef struct LibCSVRow LibCSVRow;

// Reader functions
LibCSVWrapper* libcsv_read_file(const char* filename);
LibCSVWrapper* libcsv_read_string(const char* data, size_t length);
LibCSVWrapper* libcsv_read_file_with_delimiter(const char* filename, char delimiter);
LibCSVWrapper* libcsv_read_string_with_delimiter(const char* data, size_t length, char delimiter);

// Writer functions
bool libcsv_write_file(LibCSVWrapper* csv, const char* filename);
char* libcsv_write_string(LibCSVWrapper* csv, size_t* length);

// Data access functions
size_t libcsv_get_row_count(const LibCSVWrapper* csv);
size_t libcsv_get_column_count(const LibCSVWrapper* csv, size_t row_index);
const char* libcsv_get_field(const LibCSVWrapper* csv, size_t row_index, size_t column_index);
size_t libcsv_get_field_count(const LibCSVWrapper* csv, size_t row_index);

// Memory management
void libcsv_free(LibCSVWrapper* csv);

// Error handling
const char* libcsv_get_error(void);

#endif /* LIBCSV_WRAPPER_H */