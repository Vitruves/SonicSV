/*
 * SonicSV - High-Performance CSV Parser
 *
 * Copyright (c) 2025 JHG Natter
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * DISCLAIMER:
 * - This software is provided for educational and research purposes
 * - Use at your own risk - no warranties of any kind are provided
 * - The authors are not responsible for any damages or data loss
 * - Always validate and sanitize input data when parsing untrusted sources
 * - This library performs intensive memory operations - test thoroughly in your environment
 * - SIMD optimizations may behave differently across CPU architectures
 *
 * USAGE EXAMPLE:
 *
 * #define SONICSV_IMPLEMENTATION
 * #include "sonicsv.h"
 *
 * void row_callback(const csv_row_t *row, void *user_data) {
 *     for (size_t i = 0; i < row->num_fields; i++) {
 *         const csv_field_t *field = csv_get_field(row, i);
 *         printf("Field %zu: '%.*s'\n", i, (int)field->size, field->data);
 *     }
 * }
 *
 * int main() {
 *     // Create parser with default options
 *     csv_parser_t *parser = csv_parser_create(NULL);
 *     if (!parser) {
 *         fprintf(stderr, "Failed to create parser\n");
 *         return 1;
 *     }
 *
 *     // Set callback to process each row
 *     csv_parser_set_row_callback(parser, row_callback, NULL);
 *
 *     // Parse a CSV string
 *     const char *csv_data = "name,age,city\nJohn,25,New York\nJane,30,Boston\n";
 *     csv_error_t result = csv_parse_string(parser, csv_data);
 *
 *     if (result != CSV_OK) {
 *         fprintf(stderr, "Parse error: %s\n", csv_error_string(result));
 *         csv_parser_destroy(parser);
 *         return 1;
 *     }
 *
 *     // Print performance statistics
 *     csv_print_stats(parser);
 *
 *     // Clean up
 *     csv_parser_destroy(parser);
 *     return 0;
 * }
 *
 * CUSTOM OPTIONS EXAMPLE:
 *
 * csv_parse_options_t opts = csv_default_options();
 * opts.delimiter = ';';              // Use semicolon as delimiter
 * opts.trim_whitespace = true;       // Trim whitespace from fields
 * opts.strict_mode = true;           // Enable strict parsing
 * opts.max_field_size = 1024 * 1024; // 1MB max field size
 *
 * csv_parser_t *parser = csv_parser_create(&opts);
 *
 * FILE PARSING EXAMPLE:
 *
 * csv_error_t result = csv_parse_file(parser, "data.csv");
 * if (result != CSV_OK) {
 *     fprintf(stderr, "File parse error: %s\n", csv_error_string(result));
 * }
 *
 * STREAMING EXAMPLE:
 *
 * FILE *fp = fopen("large_file.csv", "rb");
 * if (fp) {
 *     csv_error_t result = csv_parse_stream(parser, fp);
 *     fclose(fp);
 * }
 *
 * ERROR HANDLING EXAMPLE:
 *
 * void error_callback(csv_error_t error, const char *message,
 *                     uint64_t row_number, void *user_data) {
 *     fprintf(stderr, "Error on row %llu: %s (%s)\n",
 *             row_number, message, csv_error_string(error));
 * }
 *
 * csv_parser_set_error_callback(parser, error_callback, NULL);
 */

 #ifndef SONICSV_H
 #define SONICSV_H
 
 #ifndef _POSIX_C_SOURCE
 #define _POSIX_C_SOURCE 200112L
 #endif
 
 #ifndef _ISOC11_SOURCE
 #define _ISOC11_SOURCE
 #endif
 
 #include <stdbool.h>
 #include <stddef.h>
 #include <stdint.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <time.h>
 #include <stdatomic.h>
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
 #define SONICSV_VERSION_MAJOR 3
 #define SONICSV_VERSION_MINOR 1
 #define SONICSV_VERSION_PATCH 1
 
 // Enhanced compiler hints and target attributes for better SIMD compilation
 #if defined(__GNUC__) || defined(__clang__)
 #define sonicsv_likely(x)   __builtin_expect(!!(x), 1)
 #define sonicsv_unlikely(x) __builtin_expect(!!(x), 0)
 #define sonicsv_always_inline __attribute__((always_inline)) inline
 #define sonicsv_prefetch_read(ptr, offset) __builtin_prefetch((char*)(ptr) + (offset), 0, 3)
 #define sonicsv_prefetch_write(ptr, offset) __builtin_prefetch((char*)(ptr) + (offset), 1, 3)
 #define sonicsv_assume_aligned(ptr, align) __builtin_assume_aligned(ptr, align)
 #define sonicsv_force_inline __attribute__((always_inline, flatten)) inline
 #define sonicsv_hot __attribute__((hot))
 #define sonicsv_cold __attribute__((cold))
 #define sonicsv_aligned(n) __attribute__((aligned(n)))
 #else
 #define sonicsv_likely(x)   (x)
 #define sonicsv_unlikely(x) (x)
 #define sonicsv_always_inline inline
 #define sonicsv_prefetch_read(ptr, offset) ((void)0)
 #define sonicsv_prefetch_write(ptr, offset) ((void)0)
 #define sonicsv_assume_aligned(ptr, align) (ptr)
 #define sonicsv_force_inline inline
 #define sonicsv_hot
 #define sonicsv_cold
 #define sonicsv_aligned(n)
 #endif
 
 // Cache line size for optimal prefetching
 #define SONICSV_CACHE_LINE_SIZE 64
 #define SONICSV_PREFETCH_DISTANCE (8 * SONICSV_CACHE_LINE_SIZE)
 
 // SIMD feature flags
 #define CSV_SIMD_NONE 0x00
 #define CSV_SIMD_SSE4_2 0x01
 #define CSV_SIMD_AVX2 0x02
 #define CSV_SIMD_NEON 0x04
 #define CSV_SIMD_AVX512 0x08
 #define CSV_SIMD_SVE 0x10
 
 // Endianness detection
 #if defined(__BYTE_ORDER__)
     #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
         #define SONICSV_LITTLE_ENDIAN 1
         #define SONICSV_BIG_ENDIAN 0
     #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
         #define SONICSV_LITTLE_ENDIAN 0
         #define SONICSV_BIG_ENDIAN 1
     #endif
 #elif defined(__LITTLE_ENDIAN__) || defined(__ARMEL__) || defined(__THUMBEL__) || \
       defined(__AARCH64EL__) || defined(_MIPSEL) || defined(__MIPSEL) || defined(__MIPSEL__) || \
       defined(__x86_64__) || defined(__i386__) || defined(_X86_) || defined(__X86__)
     #define SONICSV_LITTLE_ENDIAN 1
     #define SONICSV_BIG_ENDIAN 0
 #else
     #define SONICSV_LITTLE_ENDIAN 0
     #define SONICSV_BIG_ENDIAN 1
 #endif
 
 // Byte swapping utilities for big-endian support
 #if SONICSV_BIG_ENDIAN
 static sonicsv_always_inline uint16_t csv_bswap16(uint16_t x) {
     return (x << 8) | (x >> 8);
 }
 
 static sonicsv_always_inline uint32_t csv_bswap32(uint32_t x) {
     return ((x & 0xFF000000) >> 24) | ((x & 0x00FF0000) >> 8) |
            ((x & 0x0000FF00) << 8)  | ((x & 0x000000FF) << 24);
 }
 
 static sonicsv_always_inline uint64_t csv_bswap64(uint64_t x) {
     return ((x & 0xFF00000000000000ULL) >> 56) | ((x & 0x00FF000000000000ULL) >> 40) |
            ((x & 0x0000FF0000000000ULL) >> 24) | ((x & 0x000000FF00000000ULL) >> 8)  |
            ((x & 0x00000000FF000000ULL) << 8)  | ((x & 0x0000000000FF0000ULL) << 24) |
            ((x & 0x000000000000FF00ULL) << 40) | ((x & 0x00000000000000FFULL) << 56);
 }
 #endif
 
 // Architecture detection with proper alignment
 #ifdef __x86_64__
 #if defined(__GNUC__) || defined(__clang__)
 #define HAVE_SSE4_2
 #define HAVE_AVX2
 #include <immintrin.h>
 #include <nmmintrin.h>
 #include <cpuid.h>
 #endif
 #define SONICSV_SIMD_ALIGN 32
 #endif
 
 #ifdef __aarch64__
 #define HAVE_NEON
 #include <arm_neon.h>
 #define SONICSV_SIMD_ALIGN 16
 #if defined(__ARM_FEATURE_SVE)
 #define HAVE_SVE
 #include <arm_sve.h>
 #endif
 #endif
 
 #ifndef SONICSV_SIMD_ALIGN
 #define SONICSV_SIMD_ALIGN 16
 #endif
 
 // Error codes
 typedef enum {
   CSV_OK = 0,
   CSV_ERROR_INVALID_ARGS = -1,
   CSV_ERROR_OUT_OF_MEMORY = -2,
   CSV_ERROR_PARSE_ERROR = -6,
   CSV_ERROR_FIELD_TOO_LARGE = -7,
   CSV_ERROR_ROW_TOO_LARGE = -8,
   CSV_ERROR_IO_ERROR = -9
 } csv_error_t;
 
 // Enhanced parse options with memory optimization settings
 typedef struct {
   char delimiter;
   char quote_char;
   bool double_quote;
   bool trim_whitespace;
   bool ignore_empty_lines;
   bool strict_mode;
   size_t max_field_size;
   size_t max_row_size;
   size_t buffer_size;
   size_t max_memory_kb;
   size_t current_memory;
   // Performance options
   bool disable_mmap;
   int num_threads;
   bool enable_parallel;
   bool enable_prefetch;
   size_t prefetch_distance;
   bool force_alignment;
 } csv_parse_options_t;
 
 // Field representation with alignment
 typedef struct sonicsv_aligned(8) {
   const char *data;
   size_t size;
   bool quoted;
 } csv_field_t;
 
 // Row representation with alignment
 typedef struct sonicsv_aligned(8) {
   csv_field_t *fields;
   size_t num_fields;
   uint64_t row_number;
   size_t byte_offset;
 } csv_row_t;
 
 // Enhanced statistics with memory and SIMD metrics
 typedef struct sonicsv_aligned(64) {
   uint64_t total_bytes_processed;
   uint64_t total_rows_parsed;
   uint64_t total_fields_parsed;
   uint64_t parse_time_ns;
   double throughput_mbps;
   uint32_t simd_acceleration_used;
   uint32_t peak_memory_kb;
   uint32_t errors_encountered;
   // Performance counters
   struct {
     uint64_t simd_ops;
     uint64_t scalar_fallbacks;
     uint64_t reallocations;
     uint64_t cache_misses;
     uint64_t prefetch_hits;
     uint64_t prefetch_misses;
     uint64_t alignment_violations;
     double avg_field_size;
     double avg_row_size;
     double memory_efficiency;
   } perf;
 } csv_stats_t;
 
 // Forward declarations
 typedef struct csv_parser csv_parser_t;
 typedef struct csv_string_pool csv_string_pool_t;
 
 // Callback types
 typedef void (*csv_row_callback_t)(const csv_row_t *row, void *user_data);
 typedef void (*csv_error_callback_t)(csv_error_t error, const char *message,
                                      uint64_t row_number, void *user_data);
 
 // Core API
 csv_parser_t *csv_parser_create(const csv_parse_options_t *options);
 void csv_parser_destroy(csv_parser_t *parser);
 csv_error_t csv_parser_reset(csv_parser_t *parser);
 void csv_parser_set_row_callback(csv_parser_t *parser, csv_row_callback_t callback, void *user_data);
 void csv_parser_set_error_callback(csv_parser_t *parser, csv_error_callback_t callback, void *user_data);
 csv_error_t csv_parse_buffer(csv_parser_t *parser, const char *buffer, size_t size, bool is_final);
 csv_error_t csv_parse_file(csv_parser_t *parser, const char *filename);
 csv_error_t csv_parse_stream(csv_parser_t *parser, FILE *stream);
 csv_error_t csv_parse_string(csv_parser_t *parser, const char* csv_line);
 
 // Utility functions
 csv_parse_options_t csv_default_options(void);
 const char *csv_error_string(csv_error_t error);
 csv_stats_t csv_parser_get_stats(const csv_parser_t *parser);
 void csv_print_stats(const csv_parser_t *parser);
 uint32_t csv_get_simd_features(void);
 const csv_field_t* csv_get_field(const csv_row_t* row, size_t index);
 size_t csv_get_num_fields(const csv_row_t* row);
 
 // String pool with hash-based interning
 csv_string_pool_t *csv_string_pool_create(size_t initial_capacity);
 void csv_string_pool_destroy(csv_string_pool_t *pool);
 const char *csv_string_pool_intern(csv_string_pool_t *pool, const char *str, size_t length);
 void csv_string_pool_clear(csv_string_pool_t *pool);
 
 #ifdef __cplusplus
 }
 #endif
 
 #ifdef SONICSV_IMPLEMENTATION
 
 // Enhanced includes for new features
 #include <pthread.h>
 #ifdef _WIN32
 #include <windows.h>
 #endif
 
 // Memory allocation tracking with recycling
 typedef struct csv_alloc_header {
     size_t size;
     size_t alignment;
     uint32_t magic;
     struct csv_alloc_header* next;
     struct csv_alloc_header* prev;
 } csv_alloc_header_t;
 
 // Memory pool for recycling allocations
 typedef struct csv_memory_pool {
     void* free_blocks[16]; // Different size classes
     size_t free_counts[16];
     size_t block_sizes[16];
     bool initialized;
 } csv_memory_pool_t;
 
 // Ensure proper initialization of thread-local storage
static __thread csv_memory_pool_t g_thread_pool = {0};
static __thread bool g_thread_pool_initialized = false;
 
 #define CSV_ALLOC_MAGIC 0xDEADBEEF
 static atomic_size_t g_total_allocated = 0;
 static atomic_size_t g_peak_allocated = 0;
 static atomic_size_t g_allocation_count = 0;
 
 // Thread-safe allocation tracking
 static sonicsv_always_inline void csv_track_allocation(csv_alloc_header_t* header) {
     size_t new_total = atomic_fetch_add_explicit(&g_total_allocated, header->size, memory_order_relaxed) + header->size;
     atomic_fetch_add_explicit(&g_allocation_count, 1, memory_order_relaxed);
 
     // Update peak if necessary (racy but approximate is fine)
     size_t current_peak = atomic_load_explicit(&g_peak_allocated, memory_order_relaxed);
     while (new_total > current_peak) {
         if (atomic_compare_exchange_weak_explicit(&g_peak_allocated, &current_peak, new_total,
                                                 memory_order_relaxed, memory_order_relaxed)) {
             break;
         }
     }
 }
 
 static sonicsv_always_inline void csv_untrack_allocation(csv_alloc_header_t* header) {
     if (header->magic == CSV_ALLOC_MAGIC) {
         atomic_fetch_sub_explicit(&g_total_allocated, header->size, memory_order_relaxed);
         atomic_fetch_sub_explicit(&g_allocation_count, 1, memory_order_relaxed);
     }
 }
 
 // Initialize memory pool size classes
 static sonicsv_cold void csv_init_memory_pool(void) {
     if (g_thread_pool_initialized) return;

    // Explicitly zero-initialize the entire structure
    memset(&g_thread_pool, 0, sizeof(g_thread_pool));
 
     // Power-of-2 size classes optimized for CSV parsing
     size_t sizes[] = {64, 128, 256, 512, 1024, 2048, 4096, 8192,
                      16384, 32768, 65536, 131072, 262144, 524288, 1048576, 2097152};
 
     for (int i = 0; i < 16; i++) {
         g_thread_pool.block_sizes[i] = sizes[i];
         g_thread_pool.free_blocks[i] = NULL;
         g_thread_pool.free_counts[i] = 0;
     }
     g_thread_pool.initialized = true;
    g_thread_pool_initialized = true;
 }
 
 // Find appropriate size class
 static sonicsv_always_inline int csv_find_size_class(size_t size) {
     for (int i = 0; i < 16; i++) {
         if (size <= g_thread_pool.block_sizes[i]) return i;
     }
     return -1; // Too large for pool
 }
 
 // Try to get block from recycling pool
 static sonicsv_always_inline void* csv_pool_alloc(size_t size, size_t alignment) {
     (void)alignment; // Suppress unused parameter warning
     if (!g_thread_pool_initialized) csv_init_memory_pool();
 
     int class_idx = csv_find_size_class(size + sizeof(csv_alloc_header_t));
     if (class_idx >= 0 && g_thread_pool.free_counts[class_idx] > 0) {
         // Reuse from pool
         void* block = g_thread_pool.free_blocks[class_idx];
         g_thread_pool.free_blocks[class_idx] = *(void**)block;
         g_thread_pool.free_counts[class_idx]--;
         return block;
     }
     return NULL;
 }
 
 // Return block to recycling pool
 static sonicsv_always_inline bool csv_pool_free(void* ptr, size_t size) {
     if (!g_thread_pool_initialized) return false;
 
     int class_idx = csv_find_size_class(size + sizeof(csv_alloc_header_t));
     if (class_idx >= 0 && g_thread_pool.free_counts[class_idx] < 8) { // Limit pool size
         *(void**)ptr = g_thread_pool.free_blocks[class_idx];
         g_thread_pool.free_blocks[class_idx] = ptr;
         g_thread_pool.free_counts[class_idx]++;
         return true;
     }
     return false;
 }
 
 // Safe memory-aligned buffer allocation with tracking and recycling
 static sonicsv_always_inline void* csv_aligned_alloc(size_t size, size_t alignment) {
     if (size == 0) return NULL;
     if (alignment == 0) alignment = sizeof(void*);
 
     // Prevent integer overflow
     if (size > SIZE_MAX - sizeof(csv_alloc_header_t) - alignment) return NULL;
 
     size_t total_size = sizeof(csv_alloc_header_t) + alignment + size;
     void* raw_ptr = csv_pool_alloc(size, alignment);
 
     if (!raw_ptr) {
         // Allocate new block
 #if defined(__APPLE__)
         if (posix_memalign(&raw_ptr, alignment, total_size) != 0) return NULL;
 #else
         raw_ptr = aligned_alloc(alignment, (total_size + alignment - 1) & ~(alignment - 1));
         if (!raw_ptr) return NULL;
 #endif
     }
 
     // Initialize header
     csv_alloc_header_t* header = (csv_alloc_header_t*)raw_ptr;
     header->size = size;
     header->alignment = alignment;
     header->magic = CSV_ALLOC_MAGIC;
     header->next = NULL;
     header->prev = NULL;
 
     csv_track_allocation(header);
 
     // Return aligned user pointer
     char* user_ptr = (char*)raw_ptr + sizeof(csv_alloc_header_t);
     size_t misalignment = (uintptr_t)user_ptr % alignment;
     if (misalignment) {
         user_ptr += alignment - misalignment;
     }
 
     return user_ptr;
 }
 
 static sonicsv_always_inline void csv_aligned_free(void* ptr) {
     if (!ptr) return;
 
     // Find header by walking backwards
     char* char_ptr = (char*)ptr;
     csv_alloc_header_t* header = NULL;
 
     // Search backwards for magic number (within reasonable bounds)
     for (size_t offset = sizeof(csv_alloc_header_t); offset <= sizeof(csv_alloc_header_t) + 64; offset += sizeof(void*)) {
         csv_alloc_header_t* candidate = (csv_alloc_header_t*)(char_ptr - offset);
         if (candidate->magic == CSV_ALLOC_MAGIC) {
             header = candidate;
             break;
         }
     }
 
     if (header) {
         csv_untrack_allocation(header);
         size_t original_size = header->size;
         header->magic = 0; // Clear magic to detect double-free
 
         // Try to recycle the block
         if (!csv_pool_free(header, original_size)) {
             free(header);
         }
     } else {
         free(ptr);
     }
 }
 
 // Memory statistics
 static sonicsv_always_inline size_t csv_get_allocated_memory(void) {
     return atomic_load_explicit(&g_total_allocated, memory_order_relaxed);
 }
 
 static sonicsv_always_inline size_t csv_get_peak_memory(void) {
     return atomic_load_explicit(&g_peak_allocated, memory_order_relaxed);
 }
 
 static sonicsv_always_inline size_t csv_get_allocation_count(void) {
     return atomic_load_explicit(&g_allocation_count, memory_order_relaxed);
 }
 
 // Optimized buffer sizes based on typical CSV patterns
 #define CSV_INITIAL_FIELD_CAPACITY 512
 #define CSV_INITIAL_BUFFER_CAPACITY 16384
 #define CSV_FIELD_DATA_POOL_INITIAL 32768
 #define CSV_GROWTH_FACTOR 1.5  // Less aggressive growth to reduce memory waste
 
 // Replace global variables with thread-local or atomic operations
 static atomic_uint g_simd_features_atomic = ATOMIC_VAR_INIT(CSV_SIMD_NONE);
 static atomic_bool g_simd_initialized_atomic = ATOMIC_VAR_INIT(false);
 
 // Parser state machine
 typedef enum {
   CSV_STATE_FIELD_START,
   CSV_STATE_IN_QUOTED_FIELD,
   CSV_STATE_QUOTE_IN_QUOTED_FIELD,
 } csv_parse_state_t;
 
 // Thread-local parser statistics for isolation
 typedef struct {
   uint64_t simd_ops;
   uint64_t scalar_fallbacks;
   uint64_t cache_hits;
   uint64_t cache_misses;
 } csv_thread_stats_t;
 
 static __thread csv_thread_stats_t g_thread_stats = {0, 0, 0, 0};
 
 // Character classification lookup table for optimized parsing
 static const uint8_t csv_char_class_table[256] = {
     // Use designated initializers for clarity and avoid excess elements
     ['\t'] = 1,      // Tab is whitespace
     ['\n'] = 2,      // Newline
     ['\r'] = 2,      // Carriage return
     [' '] = 1,       // Space is whitespace
     // All other characters default to 0 (regular)
 };
 
 // Character class definitions
 #define CSV_CHAR_REGULAR      0  // Regular character
 #define CSV_CHAR_WHITESPACE   1  // Space, tab
 #define CSV_CHAR_NEWLINE      2  // \n, \r
 #define CSV_CHAR_DELIMITER    3  // Comma (updated dynamically)
 #define CSV_CHAR_QUOTE        4  // Quote character (updated dynamically)
 
 static __thread uint8_t g_char_table[256] = {0};
 static __thread bool g_char_table_initialized = false;
 
 // Initialize character classification table for current parser
 static sonicsv_cold void csv_init_char_table(char delimiter, char quote_char) {
     if (g_char_table_initialized) return;
 
     // Explicitly zero-initialize the entire table first
     memset(g_char_table, 0, sizeof(g_char_table));
     
     // Copy base table
     memcpy(g_char_table, csv_char_class_table, sizeof(csv_char_class_table));
 
     // Set delimiter and quote character
     g_char_table[(uint8_t)delimiter] = CSV_CHAR_DELIMITER;
     g_char_table[(uint8_t)quote_char] = CSV_CHAR_QUOTE;
 
     g_char_table_initialized = true;
 }
 
 // Fast character classification
 static sonicsv_force_inline uint8_t csv_classify_char(char c) {
     return g_char_table[(uint8_t)c];
 }
 
 // Internal parser structure - isolated and thread-safe
 struct csv_parser {
   csv_parse_options_t options;
   csv_row_callback_t row_callback;
   void *row_callback_data;
   csv_error_callback_t error_callback;
   void *error_callback_data;
   csv_parse_state_t state;
   char* unparsed_buffer sonicsv_aligned(SONICSV_SIMD_ALIGN);
   size_t unparsed_size;
   size_t unparsed_capacity;
   csv_field_t *fields sonicsv_aligned(SONICSV_SIMD_ALIGN);
   size_t fields_capacity;
   size_t num_fields;
   char *field_buffer sonicsv_aligned(SONICSV_SIMD_ALIGN);
   size_t field_buffer_capacity;
   size_t field_buffer_pos;
   // Optimized field data storage with better memory layout
   char *field_data_pool sonicsv_aligned(SONICSV_SIMD_ALIGN);
   size_t field_data_pool_size;
   size_t field_data_pool_capacity;
   csv_stats_t stats;
   struct timespec start_time;
   size_t peak_memory;
   uint64_t current_row_start_offset;
   // Thread isolation - each parser has its own SIMD feature cache
   uint32_t simd_features_cache;
   bool simd_cache_initialized;
   // Parser instance ID for debugging
   uint64_t instance_id;
 } sonicsv_aligned(64);
 
 // Thread-safe instance ID generation
 static _Atomic(uint64_t) g_next_parser_id = 1;
 
 // --- Enhanced Runtime SIMD Feature Detection ---
 #ifdef __x86_64__
 static sonicsv_cold uint32_t csv_detect_x86_features(void) {
     uint32_t features = CSV_SIMD_NONE;
 
 #if defined(__GNUC__) || defined(__clang__)
     unsigned int eax, ebx, ecx, edx;
 
     // Check if CPUID is supported
     if (__get_cpuid_max(0, NULL) == 0) return features;
 
     // Check basic features (SSE4.2)
     if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
         if (ecx & bit_SSE4_2) {
             // Simple feature flag check - modern OSes handle SIMD context switching
             features |= CSV_SIMD_SSE4_2;
         }
     }
 
     // Check extended features (AVX2, AVX-512)
     if (__get_cpuid_max(0, NULL) >= 7) {
         if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
             // Check AVX2 support
             if (ebx & bit_AVX2) {
                 features |= CSV_SIMD_AVX2;
             }
 
             // Check AVX-512F support
             if (ebx & (1 << 16)) {
                 features |= CSV_SIMD_AVX512;
             }
         }
     }
 #endif
     return features;
 }
 #endif
 
 #ifdef __aarch64__
 static sonicsv_cold uint32_t csv_detect_arm_features(void) {
     uint32_t features = CSV_SIMD_NONE;
 
     // NEON is standard on ARMv8-A (AArch64)
     features |= CSV_SIMD_NEON;
 
 #ifdef HAVE_SVE
     // Check for SVE support via system calls if needed
     // For now, assume SVE is available if compiled with support
     features |= CSV_SIMD_SVE;
 #endif
 
     return features;
 }
 #endif
 
 static sonicsv_cold void csv_simd_init(void) {
     if (atomic_load_explicit(&g_simd_initialized_atomic, memory_order_acquire)) {
         return;
     }
 
     uint32_t features = CSV_SIMD_NONE;
 
 #ifdef __x86_64__
     features = csv_detect_x86_features();
 #elif defined(__aarch64__)
     features = csv_detect_arm_features();
 #else
     // Fallback for other architectures
     features = CSV_SIMD_NONE;
 #endif
 
     atomic_store_explicit(&g_simd_features_atomic, features, memory_order_release);
     atomic_store_explicit(&g_simd_initialized_atomic, true, memory_order_release);
 }
 
 // --- Search result struct ---
 typedef struct { const char *pos; size_t offset; } csv_search_result_t;
 
 // --- Optimized SIMD implementations ---
 static sonicsv_force_inline csv_search_result_t csv_scalar_find_char(const char *d, size_t s, char c1, char c2, char c3, char c4) {
   for (size_t i = 0; i < s; i++) {
     char c = d[i];
     if (c == c1 || c == c2 || c == c3 || c == c4) return (csv_search_result_t){d + i, i};
   }
   return (csv_search_result_t){NULL, s};
 }
 
 #ifdef HAVE_SSE4_2
 __attribute__((target("sse4.2")))
 static sonicsv_hot csv_search_result_t csv_sse42_find_char(const char *d, size_t s, char c1, char c2, char c3, char c4) {
   if (sonicsv_unlikely(s < 16)) return csv_scalar_find_char(d, s, c1, c2, c3, c4);
 
   // Bounds check to prevent buffer overrun
   if (sonicsv_unlikely(!d || s == 0)) return (csv_search_result_t){NULL, s};
 
   __m128i v_c1 = _mm_set1_epi8(c1), v_c2 = _mm_set1_epi8(c2),
           v_c3 = _mm_set1_epi8(c3), v_c4 = _mm_set1_epi8(c4);
   size_t i = 0;
 
   // Safe loop with explicit bounds checking
   for (; i + 16 <= s && i <= s - 16; i += 16) {
     __m128i chunk = _mm_loadu_si128((__m128i const*)(d + i));
     __m128i cmp = _mm_or_si128(_mm_or_si128(
       _mm_or_si128(_mm_cmpeq_epi8(chunk, v_c1), _mm_cmpeq_epi8(chunk, v_c2)),
       _mm_cmpeq_epi8(chunk, v_c3)), _mm_cmpeq_epi8(chunk, v_c4));
     uint32_t mask = _mm_movemask_epi8(cmp);
     if (mask != 0) {
       size_t bit_pos = __builtin_ctz(mask);
       if (i + bit_pos < s) return (csv_search_result_t){d + i + bit_pos, i + bit_pos};
     }
   }
 
   // Safe tail processing
   if (i < s) {
     csv_search_result_t tail = csv_scalar_find_char(d + i, s - i, c1, c2, c3, c4);
     if (tail.pos) tail.offset += i;
     else tail.offset = s;
     return tail;
   }
 
   return (csv_search_result_t){NULL, s};
 }
 #endif
 
 #ifdef HAVE_AVX2
 __attribute__((target("avx2")))
 static sonicsv_hot csv_search_result_t csv_avx2_find_char(const char *d, size_t s, char c1, char c2, char c3, char c4) {
   if (sonicsv_unlikely(s < 32)) return csv_scalar_find_char(d, s, c1, c2, c3, c4);
 
   // Bounds check to prevent buffer overrun
   if (sonicsv_unlikely(!d || s == 0)) return (csv_search_result_t){NULL, s};
 
   __m256i v_c1 = _mm256_set1_epi8(c1), v_c2 = _mm256_set1_epi8(c2),
           v_c3 = _mm256_set1_epi8(c3), v_c4 = _mm256_set1_epi8(c4);
   size_t i = 0;
 
   // Safe loop with explicit bounds checking
   for (; i + 32 <= s && i <= s - 32; i += 32) {
     // Safe prefetch - only if we have enough remaining data
     if (i + 64 <= s) sonicsv_prefetch_read(d, i + 64);
     __m256i chunk = _mm256_loadu_si256((__m256i const*)(d + i));
     uint32_t mask = _mm256_movemask_epi8(_mm256_or_si256(_mm256_or_si256(
       _mm256_or_si256(_mm256_cmpeq_epi8(chunk, v_c1), _mm256_cmpeq_epi8(chunk, v_c2)),
       _mm256_cmpeq_epi8(chunk, v_c3)), _mm256_cmpeq_epi8(chunk, v_c4)));
     if (mask != 0) {
       size_t bit_pos = __builtin_ctz(mask);
       if (i + bit_pos < s) return (csv_search_result_t){d + i + bit_pos, i + bit_pos};
     }
   }
 
   // Safe tail processing
   if (i < s) {
     csv_search_result_t tail = csv_scalar_find_char(d + i, s - i, c1, c2, c3, c4);
     if (tail.pos) tail.offset += i;
     else tail.offset = s;
     return tail;
   }
 
   return (csv_search_result_t){NULL, s};
 }
 #endif
 
 #ifdef HAVE_NEON
 static sonicsv_hot csv_search_result_t csv_neon_find_char(const char *d, size_t s, char c1, char c2, char c3, char c4) {
   if (sonicsv_unlikely(s < 16)) return csv_scalar_find_char(d, s, c1, c2, c3, c4);
 
   // Bounds check to prevent buffer overrun
   if (sonicsv_unlikely(!d || s == 0)) return (csv_search_result_t){NULL, s};
 
   uint8x16_t v_c1 = vdupq_n_u8((uint8_t)c1), v_c2 = vdupq_n_u8((uint8_t)c2),
              v_c3 = vdupq_n_u8((uint8_t)c3), v_c4 = vdupq_n_u8((uint8_t)c4);
   size_t i = 0;
 
   // Safe loop with explicit bounds checking
   for (; i + 16 <= s && i <= s - 16; i += 16) {
     // Safe prefetch - only if we have enough remaining data
     if (i + 64 <= s) __builtin_prefetch(d + i + 64, 0, 3);
     uint8x16_t chunk = vld1q_u8((uint8_t const*)(d + i));
     uint8x16_t cmp = vorrq_u8(vorrq_u8(
       vorrq_u8(vceqq_u8(chunk, v_c1), vceqq_u8(chunk, v_c2)),
       vceqq_u8(chunk, v_c3)), vceqq_u8(chunk, v_c4));
 
     // Extract mask from comparison result - handle endianness
     uint64_t mask_lo = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 0);
     uint64_t mask_hi = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 1);
 
 #if SONICSV_BIG_ENDIAN
     mask_lo = csv_bswap64(mask_lo);
     mask_hi = csv_bswap64(mask_hi);
 #endif
 
     if (mask_lo != 0) {
       int pos = __builtin_ctzll(mask_lo) / 8;
       if (i + pos < s) return (csv_search_result_t){d + i + pos, i + pos};
     }
     if (mask_hi != 0) {
       int pos = 8 + __builtin_ctzll(mask_hi) / 8;
       if (i + pos < s) return (csv_search_result_t){d + i + pos, i + pos};
     }
   }
 
   // Safe tail processing
   if (i < s) {
     csv_search_result_t tail = csv_scalar_find_char(d + i, s - i, c1, c2, c3, c4);
     if (tail.pos) tail.offset += i;
     else tail.offset = s;
     return tail;
   }
 
   return (csv_search_result_t){NULL, s};
 }
 #endif
 
 // Vectorized quote processing
 #ifdef HAVE_NEON
 static sonicsv_force_inline size_t csv_neon_process_quotes(const char *data, size_t size, char quote_char, char *out_buffer) {
   if (size < 16) {
     // Fallback to scalar for small buffers
     size_t out_pos = 0;
     for (size_t i = 0; i < size; i++) {
       if (data[i] == quote_char && i + 1 < size && data[i + 1] == quote_char) {
         out_buffer[out_pos++] = quote_char;
         i++; // Skip the second quote
       } else {
         out_buffer[out_pos++] = data[i];
       }
     }
     return out_pos;
   }
 
   uint8x16_t v_quote = vdupq_n_u8((uint8_t)quote_char);
   size_t i = 0, out_pos = 0;
 
   for (; i + 16 <= size; i += 16) {
     uint8x16_t chunk = vld1q_u8((uint8_t const*)(data + i));
     uint8x16_t quotes = vceqq_u8(chunk, v_quote);
 
     // Check if any quotes found in this chunk
     uint64_t mask_lo = vgetq_lane_u64(vreinterpretq_u64_u8(quotes), 0);
     uint64_t mask_hi = vgetq_lane_u64(vreinterpretq_u64_u8(quotes), 1);
 
     if (mask_lo == 0 && mask_hi == 0) {
       // No quotes, copy entire chunk
       vst1q_u8((uint8_t*)(out_buffer + out_pos), chunk);
       out_pos += 16;
     } else {
       // Process byte by byte for this chunk due to quotes
       for (int j = 0; j < 16 && i + j < size; j++) {
         if (data[i + j] == quote_char && i + j + 1 < size && data[i + j + 1] == quote_char) {
           out_buffer[out_pos++] = quote_char;
           j++; // Skip next iteration
         } else {
           out_buffer[out_pos++] = data[i + j];
         }
       }
     }
   }
 
   // Process remaining bytes
   for (; i < size; i++) {
     if (data[i] == quote_char && i + 1 < size && data[i + 1] == quote_char) {
       out_buffer[out_pos++] = quote_char;
       i++;
     } else {
       out_buffer[out_pos++] = data[i];
     }
   }
 
   return out_pos;
 }
 #endif
 
 // Parser-isolated SIMD interface with per-instance caching
 static sonicsv_force_inline csv_search_result_t csv_find_special_char_with_parser(csv_parser_t *parser, const char *d, size_t s, char del, char quo, char nl, char cr) {
     // Use parser-specific cache to avoid contention
     if (sonicsv_unlikely(!parser->simd_cache_initialized)) {
         if (!atomic_load_explicit(&g_simd_initialized_atomic, memory_order_acquire)) {
             csv_simd_init();
         }
         parser->simd_features_cache = atomic_load_explicit(&g_simd_features_atomic, memory_order_acquire);
         parser->simd_cache_initialized = true;
     }
 
     uint32_t features = parser->simd_features_cache;
 
     // Dispatch to best available implementation
 #ifdef __x86_64__
     #ifdef HAVE_AVX2
     if (sonicsv_likely(features & CSV_SIMD_AVX2)) {
         g_thread_stats.simd_ops++;
         return csv_avx2_find_char(d, s, del, quo, nl, cr);
     }
     #endif
     #ifdef HAVE_SSE4_2
     if (sonicsv_likely(features & CSV_SIMD_SSE4_2)) {
         g_thread_stats.simd_ops++;
         return csv_sse42_find_char(d, s, del, quo, nl, cr);
     }
     #endif
 #elif defined(__aarch64__)
     #ifdef HAVE_NEON
     if (sonicsv_likely(features & CSV_SIMD_NEON)) {
         g_thread_stats.simd_ops++;
         return csv_neon_find_char(d, s, del, quo, nl, cr);
     }
     #endif
 #endif
 
     // Fallback to scalar implementation
     g_thread_stats.scalar_fallbacks++;
     return csv_scalar_find_char(d, s, del, quo, nl, cr);
 }
 
 // Backward compatibility wrapper
 static sonicsv_force_inline csv_search_result_t csv_find_special_char(const char *d, size_t s, char del, char quo, char nl, char cr) {
     // For cases where parser context is not available, use global cache
     static __thread uint32_t cached_features = 0;
     static __thread bool cache_initialized = false;
 
     if (sonicsv_unlikely(!cache_initialized)) {
         if (!atomic_load_explicit(&g_simd_initialized_atomic, memory_order_acquire)) {
             csv_simd_init();
         }
         cached_features = atomic_load_explicit(&g_simd_features_atomic, memory_order_acquire);
         cache_initialized = true;
     }
 
 #ifdef __x86_64__
     #ifdef HAVE_AVX2
     if (sonicsv_likely(cached_features & CSV_SIMD_AVX2))
         return csv_avx2_find_char(d, s, del, quo, nl, cr);
     #endif
     #ifdef HAVE_SSE4_2
     if (sonicsv_likely(cached_features & CSV_SIMD_SSE4_2))
         return csv_sse42_find_char(d, s, del, quo, nl, cr);
     #endif
 #elif defined(__aarch64__)
     #ifdef HAVE_NEON
     if (sonicsv_likely(cached_features & CSV_SIMD_NEON))
         return csv_neon_find_char(d, s, del, quo, nl, cr);
     #endif
 #endif
 
     return csv_scalar_find_char(d, s, del, quo, nl, cr);
 }
 
 // --- Enhanced helper functions ---
 // Enhanced memory allocation with overflow protection and growth strategy
 static sonicsv_force_inline csv_error_t ensure_capacity_safe(void **p, size_t *cap, size_t req, size_t el_sz, csv_parser_t *parser) {
     if (sonicsv_likely(*cap >= req)) return CSV_OK;
 
     // Check for integer overflow
     if (req > SIZE_MAX / el_sz) return CSV_ERROR_OUT_OF_MEMORY;
 
     // More conservative growth to reduce memory waste
     size_t new_cap = *cap ? (size_t)(*cap * CSV_GROWTH_FACTOR) + 1 : 64;
     if (new_cap < req) new_cap = req;
 
     // Prevent excessive memory usage
     size_t new_size = new_cap * el_sz;
     if (parser && parser->options.max_memory_kb > 0) {
         size_t current_memory = csv_get_allocated_memory();
         size_t old_size = *cap * el_sz;
         if (current_memory - old_size + new_size > parser->options.max_memory_kb * 1024) {
             return CSV_ERROR_OUT_OF_MEMORY;
         }
     }
 
     // Align to cache line boundaries for better performance
     new_cap = (new_cap + SONICSV_CACHE_LINE_SIZE - 1) & ~(SONICSV_CACHE_LINE_SIZE - 1);
     new_size = new_cap * el_sz;
 
     void *new_p = csv_aligned_alloc(new_size, SONICSV_SIMD_ALIGN);
     if (sonicsv_unlikely(!new_p)) return CSV_ERROR_OUT_OF_MEMORY;
 
     if (*p) {
         size_t copy_size = *cap * el_sz;
         // Use vectorized copy for large blocks
         if (copy_size >= 64) {
             memcpy(new_p, *p, copy_size);
         } else {
             memcpy(new_p, *p, copy_size);
         }
         csv_aligned_free(*p);
     }
 
     *p = new_p;
     *cap = new_cap;
 
     // Update parser memory tracking
     if (parser) {
         parser->peak_memory = csv_get_peak_memory();
     }
 
     return CSV_OK;
 }
 
 static sonicsv_always_inline csv_error_t ensure_capacity(void **p, size_t *cap, size_t req, size_t el_sz, csv_parser_t *parser) {
     return ensure_capacity_safe(p, cap, req, el_sz, parser);
 }
 
 
 static sonicsv_always_inline csv_error_t report_error(csv_parser_t *p, csv_error_t e, const char *m) {
   p->stats.errors_encountered++;
   if (p->error_callback) p->error_callback(e, m, p->stats.total_rows_parsed + 1, p->error_callback_data);
   return e;
 }
 
 // Optimized field processing with branch prediction hints
 static sonicsv_force_inline csv_error_t add_field(csv_parser_t *p, const char *d, size_t s, bool q) {
   if (sonicsv_unlikely(s > p->options.max_field_size))
     return report_error(p, CSV_ERROR_FIELD_TOO_LARGE, "Field size exceeds max_field_size");
 
   // Pre-allocate fields array with better prediction
   if (sonicsv_unlikely(p->num_fields >= p->fields_capacity)) {
     if (sonicsv_unlikely(ensure_capacity((void**)&p->fields, &p->fields_capacity,
                                          p->num_fields + 1, sizeof(csv_field_t), p) != CSV_OK))
       return CSV_ERROR_OUT_OF_MEMORY;
   }
 
   const char *field_data = d;
 
   if (sonicsv_unlikely(q)) {
     // Quoted field processing with optimized copying
     size_t required_size = p->field_data_pool_size + s + 1;
     if (sonicsv_unlikely(required_size > p->field_data_pool_capacity)) {
       if (sonicsv_unlikely(ensure_capacity((void**)&p->field_data_pool, &p->field_data_pool_capacity,
                                            required_size, 1, p) != CSV_OK))
         return CSV_ERROR_OUT_OF_MEMORY;
     }
 
     field_data = p->field_data_pool + p->field_data_pool_size;
 
     // Optimized copy based on field size
     if (sonicsv_likely(s <= 128)) {
       // Small field - direct copy
       memcpy((char*)field_data, d, s);
     } else {
       // Large field - use potentially vectorized memcpy
       memcpy((char*)field_data, d, s);
     }
     ((char*)field_data)[s] = '\0';
     p->field_data_pool_size = required_size;
   } else if (sonicsv_unlikely(p->options.trim_whitespace && s > 0)) {
     // Unquoted field with trimming
     const char *start = d, *end = d + s;
     while (start < end && (*start == ' ' || *start == '\t')) start++;
     while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
     field_data = start;
     s = end - start;
   }
 
   // Store field with likely branch prediction
   csv_field_t *field = &p->fields[p->num_fields++];
   field->data = field_data;
   field->size = s;
   field->quoted = q;
 
   // Update statistics with optimized calculation
   p->stats.total_fields_parsed++;
   if (sonicsv_likely(p->stats.total_fields_parsed > 1)) {
     p->stats.perf.avg_field_size += (s - p->stats.perf.avg_field_size) / p->stats.total_fields_parsed;
   } else {
     p->stats.perf.avg_field_size = s;
   }
 
   return CSV_OK;
 }
 
 static sonicsv_force_inline csv_error_t finish_row(csv_parser_t *p) {
   if (p->num_fields == 0 && p->options.ignore_empty_lines) {
     return CSV_OK;
   }
 
   size_t total_field_size = 0;
   for (size_t i = 0; i < p->num_fields; i++) total_field_size += p->fields[i].size;
   if (sonicsv_unlikely(total_field_size > p->options.max_row_size))
     return report_error(p, CSV_ERROR_ROW_TOO_LARGE, "Row size exceeds max_row_size");
 
   p->stats.total_rows_parsed++;
   if (p->stats.total_rows_parsed > 0) {
     p->stats.perf.avg_row_size = (p->stats.perf.avg_row_size * (p->stats.total_rows_parsed - 1) + total_field_size) / p->stats.total_rows_parsed;
   }
 
   if (p->row_callback) {
     csv_row_t row = {p->fields, p->num_fields, p->stats.total_rows_parsed, p->current_row_start_offset};
     p->row_callback(&row, p->row_callback_data);
   }
 
   p->num_fields = 0;
   return CSV_OK;
 }
 
 static sonicsv_force_inline csv_error_t append_to_field_buffer(csv_parser_t *p, const char *s, size_t l) {
   if (sonicsv_unlikely(p->field_buffer_pos + l > p->options.max_field_size))
     return CSV_ERROR_FIELD_TOO_LARGE;
   if (sonicsv_unlikely(ensure_capacity((void**)&p->field_buffer, &p->field_buffer_capacity,
                                        p->field_buffer_pos + l, 1, p) != CSV_OK))
     return CSV_ERROR_OUT_OF_MEMORY;
 
   memcpy(p->field_buffer + p->field_buffer_pos, s, l);
   p->field_buffer_pos += l;
   return CSV_OK;
 }
 
 // --- FIXED CSV Parsing Logic ---
 csv_error_t csv_parse_buffer(csv_parser_t *p, const char *buf, size_t sz, bool is_final) {
   // Enhanced parameter validation
   if (!p) return CSV_ERROR_INVALID_ARGS;
   if (sz > 0 && !buf) return CSV_ERROR_INVALID_ARGS;
   if (sz > SIZE_MAX / 2) return CSV_ERROR_INVALID_ARGS; // Prevent overflow
   if (p->options.max_field_size == 0) return CSV_ERROR_INVALID_ARGS;
   if (p->options.max_row_size == 0) return CSV_ERROR_INVALID_ARGS;
 
   // Handle unparsed data from previous call
   if (p->unparsed_size > 0) {
     if (sonicsv_unlikely(ensure_capacity((void**)&p->unparsed_buffer, &p->unparsed_capacity,
                                          p->unparsed_size + sz, 1, p) != CSV_OK))
       return CSV_ERROR_OUT_OF_MEMORY;
     memcpy(p->unparsed_buffer + p->unparsed_size, buf, sz);
     p->unparsed_size += sz;
     buf = p->unparsed_buffer;
     sz = p->unparsed_size;
   }
 
   const char *pos = buf, *end = buf + sz;
   csv_error_t err = CSV_OK;
   const char delimiter = p->options.delimiter, quote_char = p->options.quote_char;
   size_t original_bytes_processed = p->stats.total_bytes_processed;
 
   // Optimized main parsing loop with better branch prediction
   while (pos < end && sonicsv_likely(err == CSV_OK)) {
     char c = *pos;
 
     // Use computed goto for better branch prediction (if supported)
     switch (p->state) {
       case CSV_STATE_FIELD_START:
         // Optimize common case: unquoted field
         if (sonicsv_likely(c != quote_char && c != delimiter && c != '\n' && c != '\r')) {
           // Fast path: start of unquoted field - use SIMD to find end
           const char *field_start = pos;
 
           csv_search_result_t res = csv_find_special_char_with_parser(p, pos, end - pos, delimiter, quote_char, '\n', '\r');
 
           if (res.pos == NULL) {
             if (is_final) {
               // Add final field and finish row
               if (sonicsv_unlikely((err = add_field(p, field_start, end - field_start, false)) != CSV_OK)) break;
               pos = end;
               if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
             } else {
               // Need more data - save state
               size_t consumed = field_start - buf;
               size_t remaining = end - field_start;
 
               if (buf == p->unparsed_buffer) {
                 memmove(p->unparsed_buffer, field_start, remaining);
               } else {
                 if (sonicsv_unlikely(ensure_capacity((void**)&p->unparsed_buffer, &p->unparsed_capacity,
                                                      remaining, 1, p) != CSV_OK))
                   return CSV_ERROR_OUT_OF_MEMORY;
                 memcpy(p->unparsed_buffer, field_start, remaining);
               }
               p->unparsed_size = remaining;
               p->stats.total_bytes_processed = original_bytes_processed + consumed;
               return CSV_OK;
             }
           } else {
             pos = res.pos;
 
             // Add the field
             if (sonicsv_unlikely((err = add_field(p, field_start, pos - field_start, false)) != CSV_OK)) break;
 
             // Handle what we found
             c = *pos;
             if (c == delimiter) {
               pos++;
             } else if (c == '\n') {
               pos++;
               if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
               p->current_row_start_offset = original_bytes_processed + (pos - buf);
             } else if (c == '\r') {
               pos++;
               if (pos < end && *pos == '\n') pos++; // Skip LF in CRLF
               if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
               p->current_row_start_offset = original_bytes_processed + (pos - buf);
             } else if (c == quote_char && p->options.strict_mode) {
               err = report_error(p, CSV_ERROR_PARSE_ERROR, "Quote character in unquoted field");
               break;
             }
           }
         } else if (c == quote_char) {
           p->state = CSV_STATE_IN_QUOTED_FIELD;
           p->field_buffer_pos = 0;
           pos++;
         } else if (c == delimiter) {
           if (sonicsv_unlikely((err = add_field(p, "", 0, false)) != CSV_OK)) break;
           pos++;
         } else if (c == '\n') {
           if (sonicsv_unlikely((err = add_field(p, "", 0, false)) != CSV_OK)) break;
           if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
           p->current_row_start_offset = original_bytes_processed + (pos + 1 - buf);
           pos++;
         } else if (c == '\r') {
           if (sonicsv_unlikely((err = add_field(p, "", 0, false)) != CSV_OK)) break;
           pos++;
           if (pos < end && *pos == '\n') pos++; // Skip LF in CRLF
           if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
           p->current_row_start_offset = original_bytes_processed + (pos - buf);
         }
         break;
 
       case CSV_STATE_IN_QUOTED_FIELD:
         {
           const char *chunk_start = pos;
 
           // Optimized quote scanning - look for quotes in larger chunks
           const char *quote_pos = pos;
           while (quote_pos < end && *quote_pos != quote_char) {
             quote_pos++;
           }
 
           if (quote_pos > chunk_start) {
             if (sonicsv_unlikely((err = append_to_field_buffer(p, chunk_start, quote_pos - chunk_start)) != CSV_OK)) break;
           }
 
           if (quote_pos == end) {
             if (sonicsv_unlikely(is_final)) {
               if (sonicsv_unlikely(p->options.strict_mode)) {
                 err = report_error(p, CSV_ERROR_PARSE_ERROR, "Unclosed quoted field");
                 break;
               } else {
                 // Accept unclosed quote in non-strict mode
                 if (sonicsv_unlikely((err = add_field(p, p->field_buffer, p->field_buffer_pos, true)) != CSV_OK)) break;
                 if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
                 pos = quote_pos;
                 break;
               }
             } else {
               // Save state for continuation - optimize for common case
               size_t consumed = chunk_start - buf;
               size_t remaining = end - chunk_start;
 
               if (sonicsv_likely(buf == p->unparsed_buffer)) {
                 memmove(p->unparsed_buffer, chunk_start, remaining);
               } else {
                 if (sonicsv_unlikely(ensure_capacity((void**)&p->unparsed_buffer, &p->unparsed_capacity,
                                                      remaining, 1, p) != CSV_OK))
                   return CSV_ERROR_OUT_OF_MEMORY;
                 memcpy(p->unparsed_buffer, chunk_start, remaining);
               }
               p->unparsed_size = remaining;
               p->stats.total_bytes_processed = original_bytes_processed + consumed;
               return CSV_OK;
             }
           }
 
           // Found closing quote
           pos = quote_pos + 1; // Skip the quote
           if (sonicsv_unlikely(p->options.double_quote && pos < end && *pos == quote_char)) {
             // Escaped quote - handle efficiently
             if (sonicsv_unlikely((err = append_to_field_buffer(p, &quote_char, 1)) != CSV_OK)) break;
             pos++; // Skip second quote
           } else {
             p->state = CSV_STATE_QUOTE_IN_QUOTED_FIELD;
           }
         }
         break;
 
       case CSV_STATE_QUOTE_IN_QUOTED_FIELD:
         if (c == delimiter) {
           if (sonicsv_unlikely((err = add_field(p, p->field_buffer, p->field_buffer_pos, true)) != CSV_OK)) break;
           p->state = CSV_STATE_FIELD_START;
           pos++;
         } else if (c == '\n') {
           if (sonicsv_unlikely((err = add_field(p, p->field_buffer, p->field_buffer_pos, true)) != CSV_OK)) break;
           if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
           p->state = CSV_STATE_FIELD_START;
           p->current_row_start_offset = original_bytes_processed + (pos + 1 - buf);
           pos++;
         } else if (c == '\r') {
           if (sonicsv_unlikely((err = add_field(p, p->field_buffer, p->field_buffer_pos, true)) != CSV_OK)) break;
           pos++;
           if (pos < end && *pos == '\n') pos++; // Skip LF in CRLF
           if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
           p->state = CSV_STATE_FIELD_START;
           p->current_row_start_offset = original_bytes_processed + (pos - buf);
         } else if (c == ' ' || c == '\t') {
           pos++;
         } else if (p->options.strict_mode) {
           err = report_error(p, CSV_ERROR_PARSE_ERROR, "Unexpected character after closing quote");
           break;
         } else {
           if (sonicsv_unlikely((err = append_to_field_buffer(p, &quote_char, 1)) != CSV_OK)) break;
           if (sonicsv_unlikely((err = append_to_field_buffer(p, &c, 1)) != CSV_OK)) break;
           p->state = CSV_STATE_IN_QUOTED_FIELD;
           pos++;
         }
         break;
     }
   }
 
   // FIXED: Handle final row when we reach end of input
   if (is_final && err == CSV_OK && p->num_fields > 0) {
     err = finish_row(p);
   }
 
   // Update statistics
   p->stats.total_bytes_processed = original_bytes_processed + (pos - buf);
 
   // Clear unparsed buffer if we consumed everything
   if (buf == p->unparsed_buffer && pos == end) {
     p->unparsed_size = 0;
   }
 
   return err;
 }
 
 // --- Public API Implementation ---
 csv_parse_options_t csv_default_options(void) {
     const char* jobs_env = getenv("SONICSV_JOBS");
     int num_threads = jobs_env ? atoi(jobs_env) : 1;
 
     return (csv_parse_options_t){
         .delimiter = ',', .quote_char = '"', .double_quote = true,
         .trim_whitespace = false, .ignore_empty_lines = true, .strict_mode = false,
         .max_field_size = 10 * 1024 * 1024, .max_row_size = 100 * 1024 * 1024,
         .buffer_size = 64 * 1024, .max_memory_kb = 0, .current_memory = 0,
         .disable_mmap = false, .num_threads = num_threads, .enable_parallel = true,
         .enable_prefetch = true, .prefetch_distance = SONICSV_PREFETCH_DISTANCE,
         .force_alignment = true
     };
 }
 
 const char *csv_error_string(csv_error_t error) {
   switch (error) {
     case CSV_OK: return "Success";
     case CSV_ERROR_INVALID_ARGS: return "Invalid arguments";
     case CSV_ERROR_OUT_OF_MEMORY: return "Out of memory";
     case CSV_ERROR_PARSE_ERROR: return "Parse error";
     case CSV_ERROR_FIELD_TOO_LARGE: return "Field too large";
     case CSV_ERROR_ROW_TOO_LARGE: return "Row too large";
     case CSV_ERROR_IO_ERROR: return "I/O error";
     default: return "Unknown error";
   }
 }
 
 csv_parser_t *csv_parser_create(const csv_parse_options_t *options) {
   csv_parser_t *p = csv_aligned_alloc(sizeof(csv_parser_t), 64);
   if (!p) return NULL;
 
   memset(p, 0, sizeof(csv_parser_t));
   p->options = options ? *options : csv_default_options();
   p->state = CSV_STATE_FIELD_START;
   p->instance_id = atomic_fetch_add_explicit(&g_next_parser_id, 1, memory_order_relaxed);
   p->simd_cache_initialized = false;
  
  // Explicitly initialize all pointer fields to NULL
  p->unparsed_buffer = NULL;
  p->fields = NULL;
  p->field_buffer = NULL;
  p->field_data_pool = NULL;
  p->row_callback = NULL;
  p->row_callback_data = NULL;
  p->error_callback = NULL;
  p->error_callback_data = NULL;
  
  // Initialize all size fields to 0
  p->unparsed_size = 0;
  p->unparsed_capacity = 0;
  p->fields_capacity = 0;
  p->num_fields = 0;
  p->field_buffer_capacity = 0;
  p->field_buffer_pos = 0;
  p->field_data_pool_size = 0;
  p->field_data_pool_capacity = 0;
  p->peak_memory = 0;
  p->current_row_start_offset = 0;
  p->simd_features_cache = 0;
 
   // Initialize character classification table
   csv_init_char_table(p->options.delimiter, p->options.quote_char);
 
   if (ensure_capacity((void**)&p->fields, &p->fields_capacity, CSV_INITIAL_FIELD_CAPACITY, sizeof(csv_field_t), p) != CSV_OK ||
       ensure_capacity((void**)&p->field_buffer, &p->field_buffer_capacity, CSV_INITIAL_BUFFER_CAPACITY, 1, p) != CSV_OK ||
       ensure_capacity((void**)&p->field_data_pool, &p->field_data_pool_capacity, CSV_FIELD_DATA_POOL_INITIAL, 1, p) != CSV_OK) {
     csv_parser_destroy(p);
     return NULL;
   }
 
   csv_simd_init();
   clock_gettime(CLOCK_MONOTONIC, &p->start_time);
   return p;
 }
 
 void csv_parser_destroy(csv_parser_t *p) {
   if (!p) return;
   csv_aligned_free(p->fields);
   csv_aligned_free(p->field_buffer);
   csv_aligned_free(p->unparsed_buffer);
   csv_aligned_free(p->field_data_pool);
   csv_aligned_free(p);
 }
 
 csv_error_t csv_parser_reset(csv_parser_t *p) {
   if (!p) return CSV_ERROR_INVALID_ARGS;
   p->state = CSV_STATE_FIELD_START;
   p->num_fields = 0;
   p->field_buffer_pos = 0;
   p->unparsed_size = 0;
   p->field_data_pool_size = 0;
   p->current_row_start_offset = 0;
   // Keep SIMD cache initialized across resets
   memset(&p->stats, 0, sizeof(p->stats));
   clock_gettime(CLOCK_MONOTONIC, &p->start_time);
   return CSV_OK;
 }
 
 void csv_parser_set_row_callback(csv_parser_t *p, csv_row_callback_t cb, void *ud) {
   if (!p) return;
   p->row_callback = cb;
   p->row_callback_data = ud;
 }
 
 void csv_parser_set_error_callback(csv_parser_t *p, csv_error_callback_t cb, void *ud) {
   if (!p) return;
   p->error_callback = cb;
   p->error_callback_data = ud;
 }
 
 csv_error_t csv_parse_file(csv_parser_t *p, const char *filename) {
     // Enhanced parameter validation
     if (!p) return CSV_ERROR_INVALID_ARGS;
     if (!filename || strlen(filename) == 0) return CSV_ERROR_INVALID_ARGS;
     if (strlen(filename) > 4096) return CSV_ERROR_INVALID_ARGS; // Reasonable path limit
 
     FILE *f = fopen(filename, "rb");
     if (!f) return report_error(p, CSV_ERROR_IO_ERROR, "Could not open file");
     csv_error_t result = csv_parse_stream(p, f);
     fclose(f);
     return result;
 }
 
 csv_error_t csv_parse_stream(csv_parser_t *p, FILE *stream) {
   // Enhanced parameter validation
   if (!p) return CSV_ERROR_INVALID_ARGS;
   if (!stream) return CSV_ERROR_INVALID_ARGS;
   if (ferror(stream)) return CSV_ERROR_IO_ERROR; // Check stream state
 
   char *buffer = csv_aligned_alloc(p->options.buffer_size, SONICSV_SIMD_ALIGN);
   if (!buffer) return CSV_ERROR_OUT_OF_MEMORY;
 
   csv_error_t err = CSV_OK;
   size_t bytes_read;
   while ((bytes_read = fread(buffer, 1, p->options.buffer_size, stream)) > 0) {
     if ((err = csv_parse_buffer(p, buffer, bytes_read, feof(stream))) != CSV_OK) break;
   }
 
   if (err == CSV_OK) err = csv_parse_buffer(p, "", 0, true);
   if (ferror(stream)) err = report_error(p, CSV_ERROR_IO_ERROR, "File read error");
   csv_aligned_free(buffer);
   return err;
 }
 
 csv_error_t csv_parse_string(csv_parser_t *p, const char *csv_line) {
     // Enhanced parameter validation
     if (!p) return CSV_ERROR_INVALID_ARGS;
     if (!csv_line) return CSV_ERROR_INVALID_ARGS;
     size_t len = strlen(csv_line);
     if (len > p->options.max_row_size) return CSV_ERROR_ROW_TOO_LARGE;
     return csv_parse_buffer(p, csv_line, len, true);
 }
 
 csv_stats_t csv_parser_get_stats(const csv_parser_t *p) {
   if (!p) return (csv_stats_t){0};
   csv_stats_t stats = p->stats;
   struct timespec current_time;
   clock_gettime(CLOCK_MONOTONIC, &current_time);
   uint64_t elapsed_ns = (current_time.tv_sec - p->start_time.tv_sec) * 1000000000ULL +
                         (current_time.tv_nsec - p->start_time.tv_nsec);
   stats.parse_time_ns = elapsed_ns;
   if (elapsed_ns > 0) stats.throughput_mbps = (stats.total_bytes_processed / (1024.0 * 1024.0)) / (elapsed_ns / 1e9);
   stats.simd_acceleration_used = atomic_load_explicit(&g_simd_features_atomic, memory_order_acquire);
   stats.peak_memory_kb = (p->fields_capacity * sizeof(csv_field_t) + p->field_buffer_capacity +
                          p->unparsed_capacity + p->field_data_pool_capacity) / 1024;
 
   if (stats.peak_memory_kb > 0) {
     stats.perf.memory_efficiency = (double)stats.total_bytes_processed / (stats.peak_memory_kb * 1024);
   }
 
   return stats;
 }
 
 void csv_print_stats(const csv_parser_t *p) {
   if (!p) return;
   csv_stats_t s = csv_parser_get_stats(p);
   printf("--- SonicSV Parser Statistics ---\n"
          "  Bytes Processed: %llu\n  Rows Parsed:     %llu\n  Fields Parsed:   %llu\n"
          "  Parse Time:      %.3f ms\n  Throughput:      %.2f MB/s\n"
          "  Peak Memory:     %u KB\n  Errors:          %u\n"
          "  SIMD Operations: %llu\n  Scalar Fallbacks: %llu\n"
          "  SIMD Features:   ",
          (unsigned long long)s.total_bytes_processed, (unsigned long long)s.total_rows_parsed,
          (unsigned long long)s.total_fields_parsed, s.parse_time_ns / 1e6, s.throughput_mbps,
          s.peak_memory_kb, s.errors_encountered,
          (unsigned long long)s.perf.simd_ops, (unsigned long long)s.perf.scalar_fallbacks);
   if (s.simd_acceleration_used == CSV_SIMD_NONE) printf("None");
   else {
     if (s.simd_acceleration_used & CSV_SIMD_AVX512) printf("AVX-512 ");
     if (s.simd_acceleration_used & CSV_SIMD_AVX2) printf("AVX2 ");
     if (s.simd_acceleration_used & CSV_SIMD_SSE4_2) printf("SSE4.2 ");
     if (s.simd_acceleration_used & CSV_SIMD_NEON) printf("NEON ");
     if (s.simd_acceleration_used & CSV_SIMD_SVE) printf("SVE ");
   }
   printf("\n---------------------------------\n");
 }
 
 uint32_t csv_get_simd_features(void) {
   return atomic_load_explicit(&g_simd_features_atomic, memory_order_acquire);
 }
 
 const csv_field_t *csv_get_field(const csv_row_t *row, size_t index) {
     if (!row || !row->fields || index >= row->num_fields) return NULL;
     return &row->fields[index];
 }
 
 size_t csv_get_num_fields(const csv_row_t *row) {
     if (!row) return 0;
     return row->num_fields;
 }
 
 // --- String Pool Implementation (unchanged) ---
 typedef struct {
   const char* str;
   uint32_t hash;
   size_t length;
 } csv_pool_entry_t;
 
 struct csv_string_pool {
   csv_pool_entry_t* buckets;
   uint32_t num_buckets;
   uint32_t num_items;
   char* data;
   size_t data_size;
   size_t data_capacity;
 } sonicsv_aligned(64);
 
 static sonicsv_always_inline uint32_t fnv1a_hash(const char* str, size_t len) {
   uint32_t hash = 0x811c9dc5;
   for (size_t i = 0; i < len; i++) {
     hash ^= (unsigned char)str[i];
     hash *= 0x01000193;
   }
   return hash;
 }
 
 static sonicsv_always_inline uint32_t next_power_of_2(uint32_t n) {
   if (n <= 1) return 2;
   n--;
   n |= n >> 1;
   n |= n >> 2;
   n |= n >> 4;
   n |= n >> 8;
   n |= n >> 16;
   return n + 1;
 }
 
 static csv_error_t pool_resize(csv_string_pool_t* pool) {
   uint32_t old_num_buckets = pool->num_buckets;
   csv_pool_entry_t* old_buckets = pool->buckets;
   uint32_t new_num_buckets = next_power_of_2(old_num_buckets * 2);
 
   csv_pool_entry_t* new_buckets = csv_aligned_alloc(new_num_buckets * sizeof(csv_pool_entry_t), SONICSV_SIMD_ALIGN);
   if (!new_buckets) return CSV_ERROR_OUT_OF_MEMORY;
   memset(new_buckets, 0, new_num_buckets * sizeof(csv_pool_entry_t));
 
   for (uint32_t i = 0; i < old_num_buckets; i++) {
     if (old_buckets[i].str) {
       uint32_t index = old_buckets[i].hash & (new_num_buckets - 1);
       while (new_buckets[index].str) {
         index = (index + 1) & (new_num_buckets - 1);
       }
       new_buckets[index] = old_buckets[i];
     }
   }
 
   csv_aligned_free(old_buckets);
   pool->buckets = new_buckets;
   pool->num_buckets = new_num_buckets;
   return CSV_OK;
 }
 
 csv_string_pool_t *csv_string_pool_create(size_t initial_capacity) {
   csv_string_pool_t *pool = csv_aligned_alloc(sizeof(csv_string_pool_t), 64);
   if (!pool) return NULL;
 
   memset(pool, 0, sizeof(csv_string_pool_t));
   pool->data_capacity = initial_capacity > 0 ? initial_capacity : 4096;
   pool->data = csv_aligned_alloc(pool->data_capacity, SONICSV_SIMD_ALIGN);
   pool->num_buckets = 16;
   pool->buckets = csv_aligned_alloc(pool->num_buckets * sizeof(csv_pool_entry_t), SONICSV_SIMD_ALIGN);
 
   if (!pool->data || !pool->buckets) {
     csv_string_pool_destroy(pool);
     return NULL;
   }
 
   memset(pool->buckets, 0, pool->num_buckets * sizeof(csv_pool_entry_t));
   return pool;
 }
 
 void csv_string_pool_destroy(csv_string_pool_t *pool) {
   if (!pool) return;
   csv_aligned_free(pool->buckets);
   csv_aligned_free(pool->data);
   csv_aligned_free(pool);
 }
 
 void csv_string_pool_clear(csv_string_pool_t *pool) {
   if (!pool) return;
   memset(pool->buckets, 0, pool->num_buckets * sizeof(csv_pool_entry_t));
   pool->num_items = 0;
   pool->data_size = 0;
 }
 
 const char *csv_string_pool_intern(csv_string_pool_t *pool, const char *str, size_t length) {
   if (!pool || !str) return NULL;
 
   if (pool->num_items * 4 >= pool->num_buckets * 3) {
     if (pool_resize(pool) != CSV_OK) return NULL;
   }
 
   uint32_t hash = fnv1a_hash(str, length);
   uint32_t index = hash & (pool->num_buckets - 1);
 
   while (pool->buckets[index].str) {
     if (pool->buckets[index].hash == hash &&
         pool->buckets[index].length == length &&
         memcmp(pool->buckets[index].str, str, length) == 0) {
       return pool->buckets[index].str;
     }
     index = (index + 1) & (pool->num_buckets - 1);
   }
 
   if (ensure_capacity((void**)&pool->data, &pool->data_capacity,
                      pool->data_size + length + 1, 1, NULL) != CSV_OK)
     return NULL;
 
   char* new_str = pool->data + pool->data_size;
   memcpy(new_str, str, length);
   new_str[length] = '\0';
   pool->data_size += length + 1;
 
   pool->buckets[index] = (csv_pool_entry_t){new_str, hash, length};
   pool->num_items++;
   return new_str;
 }
 
 #endif // SONICSV_IMPLEMENTATION
 
 #endif // SONICSV_H
 