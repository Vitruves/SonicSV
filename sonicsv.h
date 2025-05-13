/**
 * SonicSV - Fast, portable CSV streaming library
 * 
 * A lightweight, highly optimized CSV streaming library designed for
 * maximum portability and memory efficiency.
 * 
 * GitHub: https://github.com/Vitruves/SonicSV
 * 
 * Copyright (c) 2023 SonicSV Contributors
 * MIT License
 */

#ifndef SONICSV_H
#define SONICSV_H

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle types */
typedef struct SonicSV SonicSV;
typedef struct SonicSVRow SonicSVRow;

/**
 * Configuration structure with reasonable defaults
 */
typedef struct {
    char delimiter;         /* Field separator, default: ',' */
    char quote_char;        /* Quote character, default: '"' */
    bool trim_whitespace;   /* Whether to trim whitespace, default: false */
    bool skip_empty_lines;  /* Whether to skip empty lines, default: true */
    size_t buffer_size;     /* Read buffer size, default: 64KB */
} SonicSVConfig;

/**
 * Get the default configuration
 */
SonicSVConfig sonicsv_default_config(void);

/**
 * Open a CSV file for streaming
 * 
 * @param filename Path to CSV file
 * @param config Configuration settings (pass NULL for defaults)
 * @return SonicSV handle or NULL on error
 */
SonicSV* sonicsv_open(const char* filename, const SonicSVConfig* config);

/**
 * Close the stream and free resources
 * 
 * @param stream SonicSV handle
 */
void sonicsv_close(SonicSV* stream);

/**
 * Get the next row from the stream
 * 
 * @param stream SonicSV handle
 * @return Row handle or NULL when end of file is reached or on error
 */
SonicSVRow* sonicsv_next_row(SonicSV* stream);

/**
 * Free row resources
 * 
 * @param row SonicSVRow handle
 */
void sonicsv_row_free(SonicSVRow* row);

/**
 * Get the number of fields in the current row
 * 
 * @param row SonicSVRow handle
 * @return Number of fields
 */
size_t sonicsv_row_get_field_count(const SonicSVRow* row);

/**
 * Get a field value from the current row
 * 
 * @param row SonicSVRow handle
 * @param index Field index (0-based)
 * @return Field value or NULL if index is out of range
 */
const char* sonicsv_row_get_field(const SonicSVRow* row, size_t index);

/**
 * Get the last error message
 * 
 * @return Error message or NULL if no error
 */
const char* sonicsv_get_error(void);

#ifdef __cplusplus
}
#endif

#endif /* SONICSV_H */ 