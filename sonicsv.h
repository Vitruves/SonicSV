/**
 * SonicSV - Fast, portable CSV streaming library
 * 
 * Features:
 * - Efficient parsing with memory pooling
 * - Memory-mapped file support for large files
 * - Multithreaded processing with configurable thread pool
 * - Adaptive buffer sizing based on system characteristics
 * - Thread-safe operations with minimal locking
 * 
 * GitHub: https://github.com/Vitruves/SonicSV
 * Version: 1.3.0
 * 
 * Copyright (c) 2025 SonicSV Contributors
 * MIT License
 * 
 * DISCLAIMER: This software is provided "as is", without warranty of any kind, express or implied, including but not limited to the warranties of 
 * merchantability, fitness for a particular purpose and noninfringement. In no event shall the authors or copyright holders be liable for any claim, 
 * damages or other liability, whether in an action of contract, tort or otherwise, arising from, out of or in connection with the software or the use 
 * or other dealings in the software.
 */

#ifndef SONICSV_H
#define SONICSV_H

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * TYPES AND STRUCTURES
 *===========================================================================*/

/**
 * Opaque handle types
 */
typedef struct SonicSV SonicSV;
typedef struct SonicSVRow SonicSVRow;

/**
 * Callback function type for row processing
 * 
 * @param row The current row being processed
 * @param user_data Custom data passed to the processing function
 */
typedef void (*SonicSVRowCallback)(const SonicSVRow* row, void* user_data);

/**
 * Parser configuration options
 */
typedef struct {
    /* Basic parsing options */
    char delimiter;         /* Field separator character, default: ',' */
    char quote_char;        /* Quote character, default: '"' */
    bool trim_whitespace;   /* Whether to trim whitespace, default: false */
    bool skip_empty_lines;  /* Whether to skip empty lines, default: true */
    
    /* Memory and performance options */
    size_t buffer_size;     /* Read buffer size (0 = auto-detect based on L2 cache) */
    bool use_mmap;          /* Use memory mapping for large files, default: auto */
    bool thread_safe;       /* Whether operations are thread-safe, default: false */
    size_t thread_buffer_size; /* Per-thread buffer size (0 = auto-detect) */
    
    /* Advanced options */
    size_t max_field_capacity; /* Initial allocation capacity for fields per row (dynamic) */
    size_t max_line_length;    /* Maximum line length (0 = default reasonable limit) */
    size_t max_threads;        /* Maximum threads for parallel processing (0 = auto) */
} SonicSVConfig;

/*===========================================================================
 * CORE API FUNCTIONS
 *===========================================================================*/

/**
 * Get the default configuration with optimal settings for the current system
 * 
 * @return A configuration structure with default values
 */
SonicSVConfig sonicsv_default_config(void);

/**
 * Open a CSV file for parsing
 * 
 * @param filename Path to the CSV file
 * @param config Configuration options (NULL for defaults)
 * @return Stream handle or NULL on error
 */
SonicSV* sonicsv_open(const char* filename, const SonicSVConfig* config);

/**
 * Open a CSV from a memory buffer
 * 
 * @param data Pointer to CSV data in memory
 * @param size Size of the data in bytes
 * @param config Configuration options (NULL for defaults)
 * @return Stream handle or NULL on error
 */
SonicSV* sonicsv_open_buffer(const char* data, size_t size, const SonicSVConfig* config);

/**
 * Close the stream and free all associated resources
 * 
 * @param stream Stream handle
 */
void sonicsv_close(SonicSV* stream);

/**
 * Get the next row from the CSV stream
 * 
 * @param stream Stream handle
 * @return Row handle or NULL when end of file is reached or on error
 */
SonicSVRow* sonicsv_next_row(SonicSV* stream);

/**
 * Free a row and its associated resources
 * 
 * @param row Row handle
 */
void sonicsv_row_free(SonicSVRow* row);

/*===========================================================================
 * ROW FIELD ACCESSORS
 *===========================================================================*/

/**
 * Get the number of fields in a row
 * 
 * @param row Row handle
 * @return Number of fields
 */
size_t sonicsv_row_get_field_count(const SonicSVRow* row);

/**
 * Get a field value as a null-terminated string
 * 
 * @param row Row handle
 * @param index Field index (0-based)
 * @return Field value or NULL if index is out of range
 */
const char* sonicsv_row_get_field(const SonicSVRow* row, size_t index);

/**
 * Get a field value as raw bytes with length
 * 
 * @param row Row handle
 * @param index Field index (0-based)
 * @param length Pointer to receive the field length
 * @return Field value or NULL if index is out of range
 */
const char* sonicsv_row_get_field_bytes(const SonicSVRow* row, size_t index, size_t* length);

/*===========================================================================
 * NAVIGATION AND POSITIONING
 *===========================================================================*/

/**
 * Seek to a specific position in the file
 * 
 * @param stream Stream handle
 * @param offset Byte offset from the start of the file
 * @return true on success, false on error
 */
bool sonicsv_seek(SonicSV* stream, long offset);

/*===========================================================================
 * MULTI-THREADING SUPPORT
 *===========================================================================*/

/**
 * Initialize the thread pool with the specified number of threads
 * 
 * @param num_threads Number of threads (0 = auto-detect based on CPU cores)
 * @return true on success, false on error
 */
bool sonicsv_thread_pool_init(size_t num_threads);

/**
 * Destroy the thread pool and free associated resources
 */
void sonicsv_thread_pool_destroy(void);

/**
 * Process a CSV file in parallel using multiple threads
 * 
 * @param filename Path to the CSV file
 * @param config Configuration options (NULL for defaults)
 * @param callback Function to call for each row
 * @param user_data Custom data to pass to the callback
 * @return Number of rows processed or -1 on error
 */
long sonicsv_process_parallel(const char* filename, const SonicSVConfig* config, 
                             SonicSVRowCallback callback, void* user_data);

/*===========================================================================
 * UTILITY FUNCTIONS
 *===========================================================================*/

/**
 * Get the last error message
 * 
 * @return Error message or NULL if no error occurred
 */
const char* sonicsv_get_error(void);

/**
 * Count the number of rows in a CSV file efficiently
 * 
 * @param filename Path to the CSV file
 * @param config Configuration options (NULL for defaults)
 * @return Number of rows or -1 on error
 */
long sonicsv_count_rows(const char* filename, const SonicSVConfig* config);

/**
 * Get the optimal number of threads for the current system
 * 
 * @return Recommended number of threads
 */
size_t sonicsv_get_optimal_threads(void);

/**
 * Set the maximum memory usage limit for internal memory pools.
 * Default is SONICSV_DEFAULT_MEMORY_LIMIT (1GB).
 * 
 * @param max_bytes Maximum memory in bytes (minimum 1MB)
 * @return true if successful, false otherwise
 */
bool sonicsv_set_memory_limit(size_t max_bytes);

/**
 * Get the current library version string.
 *
 * @return A const char* to the version string.
 */
const char* sonicsv_get_version(void);

/**
 * Initialize the SonicSV library.
 * This typically sets up the global thread pool with an optimal number of threads.
 * Call this once at the start of your application if using parallel processing extensively.
 *
 * @return true on successful initialization, false otherwise.
 */
bool sonicsv_init(void);

/**
 * Clean up all global resources used by the SonicSV library.
 * This typically destroys the global thread pool.
 * Call this once when your application is shutting down.
 */
void sonicsv_cleanup(void);

/**
 * Create a new, empty SonicSVRow for manual construction.
 * Fields added to this row will be individually allocated (not from a stream's pool)
 * and must be freed by sonicsv_row_free.
 *
 * @param initial_capacity The initial number of fields the row can hold (will grow if needed).
 *                         If 0, a reasonable default is used.
 * @return A pointer to the new SonicSVRow, or NULL on allocation failure.
 */
SonicSVRow* sonicsv_row_create(size_t initial_capacity);

/**
 * Add a field to a manually created SonicSVRow.
 * The field content is copied.
 *
 * @param row The SonicSVRow to add the field to (should be created by sonicsv_row_create).
 * @param field The string content of the field.
 * @param length The length of the field string. If 0, strlen(field) is used.
 * @return true on success, false on failure (e.g., memory allocation error).
 */
bool sonicsv_row_add_field(SonicSVRow* row, const char* field, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* SONICSV_H */