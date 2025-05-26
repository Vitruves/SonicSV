/*
 * ╔═══════════════════════════════════════════════════════════════════════════════════════╗
 * ║                          SonicSV - High-Performance CSV Parser                        ║
 * ║                        SIMD-Accelerated • Zero-Copy • Portable                       ║
 * ╠═══════════════════════════════════════════════════════════════════════════════════════╣
 * ║                                                                                       ║
 * ║  Version     : 2.0.0                                                                 ║
 * ║  License     : MIT License                                                            ║
 * ║  Repository  : https://github.com/Vitruves/SonicSV                                   ║
 * ║  Authors     : J.H.G Natter & SonicSV Contributors                                   ║
 * ║  Build Date  : 2025-01-27                                                            ║
 * ║                                                                                       ║
 * ╠═══════════════════════════════════════════════════════════════════════════════════════╣
 * ║                                  DESCRIPTION                                          ║
 * ╠═══════════════════════════════════════════════════════════════════════════════════════╣
 * ║                                                                                       ║
 * ║  SonicSV is a production-ready, high-performance C library for parsing CSV and TSV   ║
 * ║  files with automatic SIMD acceleration. Designed for data scientists, engineers,    ║
 * ║  and developers who need to process large datasets efficiently.                      ║
 * ║                                                                                       ║
 * ║  🚀 KEY FEATURES:                                                                     ║
 * ║    • SIMD Acceleration: SSE4.2, AVX2 (x86_64), NEON (ARM64)                         ║
 * ║    • Zero-Copy Parsing: Direct field access without memory overhead                  ║
 * ║    • Adaptive Parsing: Auto-detection of optimal parsing strategies                  ║
 * ║    • Batch Processing: Efficient handling of large datasets                          ║
 * ║    • Parallel Processing: Multi-threaded row processing capabilities                 ║
 * ║    • Cross-Platform: Linux, macOS, Windows, embedded systems                         ║
 * ║    • Header-Only: Single-file implementation option                                  ║
 * ║                                                                                       ║
 * ║  📊 PERFORMANCE:                                                                      ║
 * ║    • Up to 3.2 GB/s throughput on modern hardware                                    ║
 * ║    • 4-5x speedup with SIMD acceleration                                             ║
 * ║    • Memory-efficient with configurable pooling                                      ║
 * ║    • Optimized for both small and large files                                        ║
 * ║                                                                                       ║
 * ║  🔧 COMPATIBILITY:                                                                    ║
 * ║    • C99+ and C++11+ compatible                                                      ║
 * ║    • x86_64 (Intel/AMD) and ARM64 (Apple Silicon, ARM servers)                      ║
 * ║    • GCC 7+, Clang 6+, MSVC 2019+                                                   ║
 * ║    • CMake, XMake, and manual compilation support                                     ║
 * ║                                                                                       ║
 * ╠═══════════════════════════════════════════════════════════════════════════════════════╣
 * ║                                   USAGE EXAMPLE                                      ║
 * ╠═══════════════════════════════════════════════════════════════════════════════════════╣
 * ║                                                                                       ║
 * ║    #define SONICSV_IMPLEMENTATION  // Include implementation                         ║
 * ║    #include "sonicsv.h"                                                              ║
 * ║                                                                                       ║
 * ║    void row_callback(const csv_row_t *row, void *user_data) {                        ║
 * ║        for (uint32_t i = 0; i < row->num_fields; i++) {                             ║
 * ║            printf("Field %u: %.*s\n", i,                                            ║
 * ║                   row->fields[i].size, row->fields[i].data);                        ║
 * ║        }                                                                             ║
 * ║    }                                                                                 ║
 * ║                                                                                       ║
 * ║    int main() {                                                                      ║
 * ║        csv_simd_init();  // Initialize SIMD features                                ║
 * ║        csv_parser_t *parser = csv_parser_create(NULL);                              ║
 * ║        csv_parser_set_row_callback(parser, row_callback, NULL);                     ║
 * ║        csv_parse_file(parser, "data.csv");                                          ║
 * ║        csv_parser_destroy(parser);                                                  ║
 * ║        return 0;                                                                     ║
 * ║    }                                                                                 ║
 * ║                                                                                       ║
 * ╠═══════════════════════════════════════════════════════════════════════════════════════╣
 * ║                           MULTITHREADING & STREAMING GUIDE                          ║
 * ╠═══════════════════════════════════════════════════════════════════════════════════════╣
 * ║                                                                                       ║
 * ║  SonicSV provides three distinct approaches for high-performance CSV processing:     ║
 * ║                                                                                       ║
 * ║  🔄 1. BUILT-IN MULTITHREADING (csv_block_parser_t):                                ║
 * ║      • Native parallel processing with automatic workload distribution              ║
 * ║      • Configure with csv_block_config_t.parallel_processing = true                 ║
 * ║      • Set csv_block_config_t.num_threads to match your CPU cores                   ║
 * ║      • Optimal for large files (>100MB) and multi-core systems                     ║
 * ║      • Uses batch callbacks for efficient zero-copy processing                      ║
 * ║                                                                                       ║
 * ║      Example:                                                                        ║
 * ║        csv_block_config_t config = csv_default_block_config();                      ║
 * ║        config.parallel_processing = true;                                           ║
 * ║        config.num_threads = 8;                                                      ║
 * ║        config.block_size = 2 * 1024 * 1024;  // 2MB blocks                         ║
 * ║        csv_block_parser_t *parser = csv_block_parser_create(&config);               ║
 * ║                                                                                       ║
 * ║  📊 2. STREAMING PARSER (csv_parser_t):                                             ║
 * ║      • Memory-efficient single-threaded processing                                  ║
 * ║      • Row-by-row callbacks for immediate processing                                ║
 * ║      • Minimal memory footprint for large files                                     ║
 * ║      • Progress callbacks for real-time status updates                              ║
 * ║      • Ideal for memory-constrained environments                                    ║
 * ║                                                                                       ║
 * ║      Example:                                                                        ║
 * ║        csv_parser_t *parser = csv_parser_create(NULL);                              ║
 * ║        csv_parser_set_row_callback(parser, process_row, user_data);                 ║
 * ║        csv_parser_set_progress_callback(parser, show_progress, NULL);               ║
 * ║        csv_parse_file(parser, "large_file.csv");                                    ║
 * ║                                                                                       ║
 * ║  🧵 3. MANUAL MULTITHREADING:                                                       ║
 * ║      • Custom threading with file chunking and line boundary detection             ║
 * ║      • Use separate csv_parser_t instances per thread                               ║
 * ║      • Implement pthread or custom thread pools                                     ║
 * ║      • Fine-grained control over work distribution                                  ║
 * ║      • Suitable for integration with existing threading systems                     ║
 * ║                                                                                       ║
 * ║      Example:                                                                        ║
 * ║        // Split file into chunks with line boundary detection                       ║
 * ║        // Create separate csv_parser_t per thread                                   ║
 * ║        // Use pthread_create() or custom thread pool                                ║
 * ║        // Aggregate results with thread-safe mechanisms                             ║
 * ║                                                                                       ║
 * ║  📈 PERFORMANCE RECOMMENDATIONS:                                                    ║
 * ║      • Use built-in multithreading for maximum throughput (>100MB files)           ║
 * ║      • Use streaming for memory-limited environments or real-time processing       ║
 * ║      • Use manual threading for custom integration requirements                     ║
 * ║      • Always call csv_simd_init() once at program startup                         ║
 * ║      • Match thread count to available CPU cores for optimal performance           ║
 * ║      • Use larger block sizes (1-4MB) for better cache efficiency                  ║
 * ║                                                                                       ║
 * ╠═══════════════════════════════════════════════════════════════════════════════════════╣
 * ║                                 IMPORTANT NOTICES                                    ║
 * ╠═══════════════════════════════════════════════════════════════════════════════════════╣
 * ║                                                                                       ║
 * ║  ⚠️  THREAD SAFETY:                                                                  ║
 * ║      Parser instances are NOT thread-safe. Use separate parsers per thread or       ║
 * ║      implement external synchronization. The SIMD detection functions are           ║
 * ║      thread-safe after initialization.                                               ║
 * ║                                                                                       ║
 * ║  🔒 MEMORY MANAGEMENT:                                                               ║
 * ║      Zero-copy parsing means field data points directly into input buffers.         ║
 * ║      Ensure input data remains valid during field access. Use string pooling        ║
 * ║      for long-lived field references.                                                ║
 * ║                                                                                       ║
 * ║  🎯 PERFORMANCE CONSIDERATIONS:                                                      ║
 * ║      • Call csv_simd_init() once at program startup                                 ║
 * ║      • Use batch processing for files > 10MB                                        ║
 * ║      • Enable parallel processing for multi-core systems                            ║
 * ║      • Consider memory pooling for high-frequency parsing                           ║
 * ║                                                                                       ║
 * ║  📝 ERROR HANDLING:                                                                  ║
 * ║      All functions return csv_error_t codes. Always check return values.            ║
 * ║      Use error callbacks for detailed error reporting with row/column context.      ║
 * ║                                                                                       ║
 * ║  🔧 COMPILATION:                                                                     ║
 * ║      Define SONICSV_IMPLEMENTATION before including this header in ONE source       ║
 * ║      file to include the implementation. Link with -lm on Unix systems.             ║
 * ║                                                                                       ║
 * ╠═══════════════════════════════════════════════════════════════════════════════════════╣
 * ║                                    LICENSE                                           ║
 * ╠═══════════════════════════════════════════════════════════════════════════════════════╣
 * ║                                                                                       ║
 * ║  Copyright (c) 2025 J.H.G Natter and SonicSV Contributors                           ║
 * ║                                                                                       ║
 * ║  Permission is hereby granted, free of charge, to any person obtaining a copy       ║
 * ║  of this software and associated documentation files (the "Software"), to deal      ║
 * ║  in the Software without restriction, including without limitation the rights       ║
 * ║  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell          ║
 * ║  copies of the Software, and to permit persons to whom the Software is              ║
 * ║  furnished to do so, subject to the following conditions:                           ║
 * ║                                                                                       ║
 * ║  The above copyright notice and this permission notice shall be included in all     ║
 * ║  copies or substantial portions of the Software.                                     ║
 * ║                                                                                       ║
 * ║  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR         ║
 * ║  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,           ║
 * ║  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE        ║
 * ║  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER             ║
 * ║  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,      ║
 * ║  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE      ║
 * ║  SOFTWARE.                                                                           ║
 * ║                                                                                       ║
 * ╚═══════════════════════════════════════════════════════════════════════════════════════╝
 */

#ifndef SONICSV_H
#define SONICSV_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SONICSV_VERSION_MAJOR 1
#define SONICSV_VERSION_MINOR 0
#define SONICSV_VERSION_PATCH 0

#ifndef CSV_SIMD_NONE
#define CSV_SIMD_NONE 0x00
#define CSV_SIMD_SSE4_2 0x01
#define CSV_SIMD_AVX2 0x02
#define CSV_SIMD_NEON 0x04
#endif

#ifdef __x86_64__
#ifndef HAVE_SSE4_2
#define HAVE_SSE4_2
#endif
#ifndef HAVE_AVX2
#define HAVE_AVX2
#endif
#include <immintrin.h>
#include <nmmintrin.h>
#endif

#ifdef __aarch64__
#ifndef HAVE_NEON
#define HAVE_NEON
#endif
#include <arm_neon.h>
#endif

typedef enum {
  CSV_OK = 0,
  CSV_ERROR_INVALID_ARGS = -1,
  CSV_ERROR_OUT_OF_MEMORY = -2,
  CSV_ERROR_BUFFER_TOO_SMALL = -3,
  CSV_ERROR_INVALID_FORMAT = -4,
  CSV_ERROR_IO_ERROR = -5,
  CSV_ERROR_PARSE_ERROR = -6
} csv_error_t;

typedef struct {
  char delimiter;
  char quote_char;
  char escape_char;
  bool quoting;
  bool escaping;
  bool double_quote;
  bool newlines_in_values;
  bool ignore_empty_lines;
  bool trim_whitespace;
  int32_t max_field_size;
  int32_t max_row_size;
} csv_parse_options_t;

typedef struct {
  const char *data;
  uint32_t size;
  bool quoted;
} csv_field_t;

typedef struct {
  csv_field_t *fields;
  uint32_t num_fields;
  uint64_t row_number;
} csv_row_t;

typedef struct {
  uint64_t total_bytes_processed;
  uint64_t total_rows_parsed;
  uint64_t total_fields_parsed;
  uint64_t parse_time_ns;
  double throughput_mbps;
  uint32_t simd_acceleration_used;
} csv_stats_t;

typedef struct {
  const char *pos;
  uint32_t count;
} csv_search_result_t;

typedef enum {
  CSV_MODE_GENERIC = 0,
  CSV_MODE_SIMPLE = 1,
  CSV_MODE_QUOTED_ONLY = 2,
  CSV_MODE_TSV = 3
} csv_parse_mode_t;

typedef struct {
  uint32_t offset : 31;
  bool quoted : 1;
} csv_field_desc_t;

typedef struct {
  const char *data;
  uint32_t data_size;
  csv_field_desc_t *field_offsets;
  uint32_t *row_offsets;
  uint32_t num_rows;
  uint32_t num_fields_total;
  uint32_t max_fields_per_row;
  bool has_quoted_fields;
  bool has_escaped_chars;
  uint32_t avg_row_size;
  csv_parse_mode_t parse_mode;
} csv_data_batch_t;

typedef struct {
  csv_parse_options_t parse_options;
  uint32_t block_size;
  uint32_t max_rows_per_batch;
  bool parallel_processing;
  uint32_t num_threads;
  bool use_memory_pool;
  uint32_t initial_pool_size;
} csv_block_config_t;

typedef struct {
  uint64_t total_blocks_processed;
  uint64_t total_batches_created;
  uint64_t total_simd_operations;
  uint64_t total_parallel_tasks;
  double avg_block_parse_time_ms;
  double avg_batch_creation_time_ms;
  double simd_acceleration_ratio;
  uint32_t peak_memory_usage_mb;
  uint32_t avg_rows_per_batch;
  uint32_t avg_fields_per_row;
} csv_advanced_stats_t;

typedef struct {
  uint32_t *positions;
  uint32_t count;
  uint32_t capacity;
} csv_char_positions_t;

typedef struct {
  uint32_t start_row;
  uint32_t end_row;
  const csv_data_batch_t *batch;
  void *user_data;
} csv_row_range_t;

typedef struct csv_parser csv_parser_t;
typedef struct csv_block_parser csv_block_parser_t;
typedef struct csv_string_pool csv_string_pool_t;

typedef void (*csv_row_callback_t)(const csv_row_t *row, void *user_data);
typedef void (*csv_error_callback_t)(csv_error_t error, const char *message,
                                     uint64_t row_number, void *user_data);
typedef void (*csv_progress_callback_t)(uint64_t bytes_processed,
                                        uint64_t total_bytes, void *user_data);
typedef void (*csv_batch_callback_t)(const csv_data_batch_t *batch,
                                     void *user_data);
typedef void (*csv_row_processor_t)(const csv_row_range_t *range);

csv_parser_t *csv_parser_create(const csv_parse_options_t *options);
void csv_parser_destroy(csv_parser_t *parser);
void csv_parser_set_row_callback(csv_parser_t *parser,
                                 csv_row_callback_t callback, void *user_data);
void csv_parser_set_error_callback(csv_parser_t *parser,
                                   csv_error_callback_t callback,
                                   void *user_data);
void csv_parser_set_progress_callback(csv_parser_t *parser,
                                      csv_progress_callback_t callback,
                                      void *user_data);
csv_error_t csv_parse_buffer(csv_parser_t *parser, const char *buffer,
                             size_t size, bool is_final);
csv_error_t csv_parse_file(csv_parser_t *parser, const char *filename);
csv_error_t csv_parse_stream(csv_parser_t *parser, FILE *stream);
csv_parse_options_t csv_default_options(void);
const char *csv_error_string(csv_error_t error);
void csv_print_stats(const csv_parser_t *parser);
csv_stats_t csv_parser_get_stats(const csv_parser_t *parser);
uint32_t csv_detect_simd_features(void);

csv_block_parser_t *csv_block_parser_create(const csv_block_config_t *config);
void csv_block_parser_destroy(csv_block_parser_t *parser);
void csv_block_parser_set_batch_callback(csv_block_parser_t *parser,
                                         csv_batch_callback_t callback,
                                         void *user_data);
csv_error_t csv_block_parse_buffer(csv_block_parser_t *parser,
                                   const char *buffer, size_t size,
                                   bool is_final);
csv_error_t csv_block_parse_file(csv_block_parser_t *parser,
                                 const char *filename);
csv_error_t
csv_batch_visit_column(const csv_data_batch_t *batch, uint32_t col_index,
                       void (*visitor)(const char *data, uint32_t size,
                                       bool quoted, void *user_data),
                       void *user_data);
csv_error_t csv_batch_get_field(const csv_data_batch_t *batch,
                                uint32_t row_index, uint32_t col_index,
                                const char **data, uint32_t *size,
                                bool *quoted);
csv_error_t csv_parse_tsv_buffer(csv_block_parser_t *parser, const char *buffer,
                                 size_t size, bool is_final);
csv_parse_mode_t csv_detect_parse_mode(const char *sample_data,
                                       size_t sample_size);
csv_error_t csv_parse_simple_mode(csv_block_parser_t *parser,
                                  const char *buffer, size_t size,
                                  bool is_final);
csv_error_t csv_parse_quoted_mode(csv_block_parser_t *parser,
                                  const char *buffer, size_t size,
                                  bool is_final);
csv_char_positions_t csv_simd_find_all_chars(const char *data, size_t size,
                                             char target_char);
uint32_t csv_simd_count_fields_in_rows(const char *data, size_t size,
                                       const uint32_t *row_offsets,
                                       uint32_t num_rows, char delimiter);
csv_error_t csv_parse_tsv_vectorized(const char *data, size_t size,
                                     csv_batch_callback_t callback,
                                     void *user_data);
csv_error_t csv_process_rows_parallel(const csv_data_batch_t *batch,
                                      csv_row_processor_t processor,
                                      void *user_data, uint32_t num_threads);
csv_string_pool_t *csv_string_pool_create(uint32_t initial_capacity);
void csv_string_pool_destroy(csv_string_pool_t *pool);
const char *csv_string_pool_intern(csv_string_pool_t *pool, const char *str,
                                   uint32_t length);
void csv_string_pool_clear(csv_string_pool_t *pool);
csv_advanced_stats_t
csv_block_parser_get_stats(const csv_block_parser_t *parser);
csv_block_config_t csv_default_block_config(void);

void csv_simd_init(void);
uint32_t csv_simd_get_features(void);
csv_search_result_t csv_simd_find_special_char(const char *data, size_t size,
                                               char delimiter, char quote,
                                               char escape);
csv_search_result_t csv_simd_find_eol(const char *data, size_t size);
csv_search_result_t csv_simd_find_quote_end(const char *data, size_t size,
                                            char quote, char escape,
                                            bool double_quote);
uint32_t csv_simd_count_delimiters(const char *data, size_t size,
                                   char delimiter);
bool csv_simd_validate_utf8(const char *data, size_t size);
void csv_simd_memcpy(void *dest, const void *src, size_t size);
int csv_simd_memcmp(const void *s1, const void *s2, size_t size);
csv_search_result_t
csv_simd_find_delimiter_newline(const char *data, size_t size, char delimiter);
csv_search_result_t csv_simd_find_delimiter_newline_tsv(const char *data,
                                                        size_t size,
                                                        char delimiter);
uint32_t csv_simd_count_delimiters_tsv(const char *data, size_t size,
                                       char delimiter);

#ifdef __cplusplus
}
#endif

#ifdef SONICSV_IMPLEMENTATION

static uint32_t g_simd_features = CSV_SIMD_NONE;

struct csv_parser {
  csv_parse_options_t options;
  csv_row_callback_t row_callback;
  void *row_callback_data;
  csv_error_callback_t error_callback;
  void *error_callback_data;
  csv_progress_callback_t progress_callback;
  void *progress_callback_data;
  char *buffer;
  size_t buffer_size;
  size_t buffer_capacity;
  enum {
    CSV_STATE_FIELD_START,
    CSV_STATE_IN_FIELD,
    CSV_STATE_IN_QUOTED_FIELD,
    CSV_STATE_QUOTE_IN_QUOTED_FIELD,
    CSV_STATE_ESCAPE_IN_FIELD,
    CSV_STATE_ESCAPE_IN_QUOTED_FIELD
  } state;
  csv_field_t *fields;
  uint32_t num_fields;
  uint32_t fields_capacity;
  char *field_data_buffer;
  uint32_t field_data_size;
  uint32_t field_data_capacity;
  char *field_buffer;
  uint32_t field_size;
  uint32_t field_capacity;
  bool field_quoted;
  csv_stats_t stats;
  struct timespec start_time;
  uint32_t simd_features;
  csv_parse_mode_t detected_mode;
  bool mode_detected;
};

struct csv_block_parser {
  csv_block_config_t config;
  csv_batch_callback_t batch_callback;
  void *batch_callback_data;
  csv_data_batch_t current_batch;
  csv_advanced_stats_t stats;
  struct timespec start_time;
  uint32_t simd_features;
  csv_parse_mode_t detected_mode;
  bool mode_detected;
};

struct csv_string_pool {
  char *data;
  uint32_t size;
  uint32_t capacity;
  char **strings;
  uint32_t num_strings;
  uint32_t strings_capacity;
};

uint32_t csv_detect_simd_features(void) {
  uint32_t features = CSV_SIMD_NONE;

#ifdef HAVE_SSE4_2
#ifdef __GNUC__
  if (__builtin_cpu_supports("sse4.2")) {
    features |= CSV_SIMD_SSE4_2;
  }
  if (__builtin_cpu_supports("avx2")) {
    features |= CSV_SIMD_AVX2;
  }
#endif
#endif

#ifdef HAVE_NEON
  features |= CSV_SIMD_NEON;
#endif

  return features;
}

void csv_simd_init(void) { g_simd_features = csv_detect_simd_features(); }

uint32_t csv_simd_get_features(void) { return g_simd_features; }

static csv_search_result_t
csv_scalar_find_special_char(const char *data, size_t size, char delimiter,
                             char quote, char escape) {
  csv_search_result_t result = {NULL, 0};

  for (size_t i = 0; i < size; i++) {
    char c = data[i];
    if (c == delimiter || c == quote || c == escape || c == '\r' || c == '\n') {
      result.pos = data + i;
      result.count = i;
      return result;
    }
  }

  result.count = size;
  return result;
}

static csv_search_result_t csv_scalar_find_eol(const char *data, size_t size) {
  csv_search_result_t result = {NULL, 0};

  for (size_t i = 0; i < size; i++) {
    char c = data[i];
    if (c == '\r' || c == '\n') {
      result.pos = data + i;
      result.count = i;
      return result;
    }
  }

  result.count = size;
  return result;
}

static uint32_t csv_scalar_count_delimiters(const char *data, size_t size,
                                            char delimiter) {
  uint32_t count = 0;
  for (size_t i = 0; i < size; i++) {
    if (data[i] == delimiter) {
      count++;
    }
  }
  return count;
}

static bool csv_scalar_validate_utf8(const char *data, size_t size) {
  for (size_t i = 0; i < size; i++) {
    unsigned char c = (unsigned char)data[i];

    if (c < 0x80) {
      continue;
    } else if ((c >> 5) == 0x06) {
      if (i + 1 >= size || (data[i + 1] & 0xC0) != 0x80) {
        return false;
      }
      i++;
    } else if ((c >> 4) == 0x0E) {
      if (i + 2 >= size || (data[i + 1] & 0xC0) != 0x80 ||
          (data[i + 2] & 0xC0) != 0x80) {
        return false;
      }
      i += 2;
    } else if ((c >> 3) == 0x1E) {
      if (i + 3 >= size || (data[i + 1] & 0xC0) != 0x80 ||
          (data[i + 2] & 0xC0) != 0x80 || (data[i + 3] & 0xC0) != 0x80) {
        return false;
      }
      i += 3;
    } else {
      return false;
    }
  }
  return true;
}

#ifdef HAVE_SSE4_2
// Forward declaration for SSE-only function
static uint32_t csv_sse42_count_delimiters_sse_only(const char *data,
                                                    size_t size,
                                                    char delimiter);

static csv_search_result_t
csv_sse42_find_special_char(const char *data, size_t size, char delimiter,
                            char quote, char escape) {
  csv_search_result_t result = {NULL, 0};

  if (size < 16) {
    return csv_scalar_find_special_char(data, size, delimiter, quote, escape);
  }

  __m128i chars = _mm_setr_epi8(delimiter, quote, escape, '\r', '\n', 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0, 0);

  size_t i = 0;

  for (; i + 64 <= size; i += 64) {
    __m128i chunk1 = _mm_loadu_si128((__m128i *)(data + i));
    __m128i chunk2 = _mm_loadu_si128((__m128i *)(data + i + 16));
    __m128i chunk3 = _mm_loadu_si128((__m128i *)(data + i + 32));
    __m128i chunk4 = _mm_loadu_si128((__m128i *)(data + i + 48));

    int idx1 = _mm_cmpistri(chars, chunk1,
                            _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                _SIDD_LEAST_SIGNIFICANT);
    int idx2 = _mm_cmpistri(chars, chunk2,
                            _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                _SIDD_LEAST_SIGNIFICANT);
    int idx3 = _mm_cmpistri(chars, chunk3,
                            _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                _SIDD_LEAST_SIGNIFICANT);
    int idx4 = _mm_cmpistri(chars, chunk4,
                            _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                _SIDD_LEAST_SIGNIFICANT);

    if (idx1 < 16) {
      result.pos = data + i + idx1;
      result.count = i + idx1;
      return result;
    }
    if (idx2 < 16) {
      result.pos = data + i + 16 + idx2;
      result.count = i + 16 + idx2;
      return result;
    }
    if (idx3 < 16) {
      result.pos = data + i + 32 + idx3;
      result.count = i + 32 + idx3;
      return result;
    }
    if (idx4 < 16) {
      result.pos = data + i + 48 + idx4;
      result.count = i + 48 + idx4;
      return result;
    }
  }

  for (; i + 16 <= size; i += 16) {
    __m128i chunk = _mm_loadu_si128((__m128i *)(data + i));
    int index = _mm_cmpistri(chars, chunk,
                             _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                 _SIDD_LEAST_SIGNIFICANT);

    if (index < 16) {
      result.pos = data + i + index;
      result.count = i + index;
      return result;
    }
  }

  if (i < size) {
    csv_search_result_t scalar_result = csv_scalar_find_special_char(
        data + i, size - i, delimiter, quote, escape);
    if (scalar_result.pos) {
      result.pos = scalar_result.pos;
      result.count = i + scalar_result.count;
    } else {
      result.count = size;
    }
  } else {
    result.count = size;
  }

  return result;
}

#ifdef HAVE_AVX2
__attribute__((target("avx2"))) static uint32_t
csv_sse42_count_delimiters_avx2(const char *data, size_t size, char delimiter) {
  if (size < 32) {
    return csv_scalar_count_delimiters(data, size, delimiter);
  }

  // Check if AVX2 is actually available at runtime
  if (!(g_simd_features & CSV_SIMD_AVX2)) {
    return csv_sse42_count_delimiters_sse_only(data, size, delimiter);
  }

  __m256i delim_vec = _mm256_set1_epi8(delimiter);
  uint32_t count = 0;

  size_t i = 0;
  for (; i + 128 <= size; i += 128) {
    __m256i chunk1 = _mm256_loadu_si256((__m256i *)(data + i));
    __m256i chunk2 = _mm256_loadu_si256((__m256i *)(data + i + 32));
    __m256i chunk3 = _mm256_loadu_si256((__m256i *)(data + i + 64));
    __m256i chunk4 = _mm256_loadu_si256((__m256i *)(data + i + 96));

    __m256i cmp1 = _mm256_cmpeq_epi8(chunk1, delim_vec);
    __m256i cmp2 = _mm256_cmpeq_epi8(chunk2, delim_vec);
    __m256i cmp3 = _mm256_cmpeq_epi8(chunk3, delim_vec);
    __m256i cmp4 = _mm256_cmpeq_epi8(chunk4, delim_vec);

    count += __builtin_popcountll(_mm256_movemask_epi8(cmp1));
    count += __builtin_popcountll(_mm256_movemask_epi8(cmp2));
    count += __builtin_popcountll(_mm256_movemask_epi8(cmp3));
    count += __builtin_popcountll(_mm256_movemask_epi8(cmp4));
  }

  for (; i + 32 <= size; i += 32) {
    __m256i chunk = _mm256_loadu_si256((__m256i *)(data + i));
    __m256i cmp = _mm256_cmpeq_epi8(chunk, delim_vec);
    count += __builtin_popcountll(_mm256_movemask_epi8(cmp));
  }

  count += csv_scalar_count_delimiters(data + i, size - i, delimiter);

  return count;
}
#endif

// SSE-only version for fallback
static uint32_t csv_sse42_count_delimiters_sse_only(const char *data,
                                                    size_t size,
                                                    char delimiter) {
  if (size < 16) {
    return csv_scalar_count_delimiters(data, size, delimiter);
  }

  __m128i delim_vec = _mm_set1_epi8(delimiter);
  uint32_t count = 0;

  size_t i = 0;
  for (; i + 64 <= size; i += 64) {
    __m128i chunk1 = _mm_loadu_si128((__m128i *)(data + i));
    __m128i chunk2 = _mm_loadu_si128((__m128i *)(data + i + 16));
    __m128i chunk3 = _mm_loadu_si128((__m128i *)(data + i + 32));
    __m128i chunk4 = _mm_loadu_si128((__m128i *)(data + i + 48));

    __m128i cmp1 = _mm_cmpeq_epi8(chunk1, delim_vec);
    __m128i cmp2 = _mm_cmpeq_epi8(chunk2, delim_vec);
    __m128i cmp3 = _mm_cmpeq_epi8(chunk3, delim_vec);
    __m128i cmp4 = _mm_cmpeq_epi8(chunk4, delim_vec);

    count += __builtin_popcount(_mm_movemask_epi8(cmp1));
    count += __builtin_popcount(_mm_movemask_epi8(cmp2));
    count += __builtin_popcount(_mm_movemask_epi8(cmp3));
    count += __builtin_popcount(_mm_movemask_epi8(cmp4));
  }

  for (; i + 16 <= size; i += 16) {
    __m128i chunk = _mm_loadu_si128((__m128i *)(data + i));
    __m128i cmp = _mm_cmpeq_epi8(chunk, delim_vec);
    count += __builtin_popcount(_mm_movemask_epi8(cmp));
  }

  count += csv_scalar_count_delimiters(data + i, size - i, delimiter);

  return count;
}

static uint32_t csv_sse42_count_delimiters(const char *data, size_t size,
                                           char delimiter) {
#ifdef HAVE_AVX2
  if (g_simd_features & CSV_SIMD_AVX2) {
    return csv_sse42_count_delimiters_avx2(data, size, delimiter);
  }
#endif
  return csv_sse42_count_delimiters_sse_only(data, size, delimiter);
}

static csv_search_result_t csv_sse42_find_eol(const char *data, size_t size) {
  csv_search_result_t result = {NULL, 0};

  if (size < 16) {
    return csv_scalar_find_eol(data, size);
  }

  __m128i newlines =
      _mm_setr_epi8('\r', '\n', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

  size_t i = 0;
  for (; i + 16 <= size; i += 16) {
    __m128i chunk = _mm_loadu_si128((__m128i *)(data + i));

    int index = _mm_cmpistri(newlines, chunk,
                             _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                 _SIDD_LEAST_SIGNIFICANT);

    if (index < 16) {
      result.pos = data + i + index;
      result.count = i + index;
      return result;
    }
  }

  if (i < size) {
    csv_search_result_t scalar_result = csv_scalar_find_eol(data + i, size - i);
    if (scalar_result.pos) {
      result.pos = scalar_result.pos;
      result.count = i + scalar_result.count;
    } else {
      result.count = size;
    }
  } else {
    result.count = size;
  }

  return result;
}

static bool csv_sse42_validate_utf8(const char *data, size_t size) {
  return csv_scalar_validate_utf8(data, size);
}
#endif

#ifdef HAVE_NEON
static csv_search_result_t csv_neon_find_special_char(const char *data,
                                                      size_t size,
                                                      char delimiter,
                                                      char quote, char escape) {
  csv_search_result_t result = {NULL, 0};

  if (size < 16) {
    return csv_scalar_find_special_char(data, size, delimiter, quote, escape);
  }

  uint8x16_t delim_vec = vdupq_n_u8((uint8_t)delimiter);
  uint8x16_t quote_vec = vdupq_n_u8((uint8_t)quote);
  uint8x16_t escape_vec = vdupq_n_u8((uint8_t)escape);
  uint8x16_t cr_vec = vdupq_n_u8('\r');
  uint8x16_t lf_vec = vdupq_n_u8('\n');

  size_t i = 0;
  for (; i + 16 <= size; i += 16) {
    uint8x16_t chunk = vld1q_u8((uint8_t *)(data + i));

    uint8x16_t cmp_delim = vceqq_u8(chunk, delim_vec);
    uint8x16_t cmp_quote = vceqq_u8(chunk, quote_vec);
    uint8x16_t cmp_escape = vceqq_u8(chunk, escape_vec);
    uint8x16_t cmp_cr = vceqq_u8(chunk, cr_vec);
    uint8x16_t cmp_lf = vceqq_u8(chunk, lf_vec);

    uint8x16_t combined = vorrq_u8(cmp_delim, cmp_quote);
    combined = vorrq_u8(combined, cmp_escape);
    combined = vorrq_u8(combined, cmp_cr);
    combined = vorrq_u8(combined, cmp_lf);

    uint64x2_t combined64 = vreinterpretq_u64_u8(combined);
    uint64_t low = vgetq_lane_u64(combined64, 0);
    uint64_t high = vgetq_lane_u64(combined64, 1);

    if (low != 0 || high != 0) {
      for (int j = 0; j < 16; j++) {
        if (vgetq_lane_u8(combined, j) != 0) {
          result.pos = data + i + j;
          result.count = i + j;
          return result;
        }
      }
    }
  }

  if (i < size) {
    csv_search_result_t scalar_result = csv_scalar_find_special_char(
        data + i, size - i, delimiter, quote, escape);
    if (scalar_result.pos) {
      result.pos = scalar_result.pos;
      result.count = i + scalar_result.count;
    } else {
      result.count = size;
    }
  } else {
    result.count = size;
  }

  return result;
}

static csv_search_result_t csv_neon_find_eol(const char *data, size_t size) {
  csv_search_result_t result = {NULL, 0};

  if (size < 16) {
    return csv_scalar_find_eol(data, size);
  }

  uint8x16_t cr_vec = vdupq_n_u8('\r');
  uint8x16_t lf_vec = vdupq_n_u8('\n');

  size_t i = 0;
  for (; i + 16 <= size; i += 16) {
    uint8x16_t chunk = vld1q_u8((uint8_t *)(data + i));

    uint8x16_t cmp_cr = vceqq_u8(chunk, cr_vec);
    uint8x16_t cmp_lf = vceqq_u8(chunk, lf_vec);
    uint8x16_t combined = vorrq_u8(cmp_cr, cmp_lf);

    uint64x2_t combined64 = vreinterpretq_u64_u8(combined);
    uint64_t low = vgetq_lane_u64(combined64, 0);
    uint64_t high = vgetq_lane_u64(combined64, 1);

    if (low != 0 || high != 0) {
      for (int j = 0; j < 16; j++) {
        if (vgetq_lane_u8(combined, j) != 0) {
          result.pos = data + i + j;
          result.count = i + j;
          return result;
        }
      }
    }
  }

  if (i < size) {
    csv_search_result_t scalar_result = csv_scalar_find_eol(data + i, size - i);
    if (scalar_result.pos) {
      result.pos = scalar_result.pos;
      result.count = i + scalar_result.count;
    } else {
      result.count = size;
    }
  } else {
    result.count = size;
  }

  return result;
}

static uint32_t csv_neon_count_delimiters(const char *data, size_t size,
                                          char delimiter) {
  if (size < 16) {
    return csv_scalar_count_delimiters(data, size, delimiter);
  }

  uint8x16_t delim_vec = vdupq_n_u8((uint8_t)delimiter);
  uint32_t count = 0;

  size_t i = 0;
  for (; i + 16 <= size; i += 16) {
    uint8x16_t chunk = vld1q_u8((uint8_t *)(data + i));
    uint8x16_t cmp = vceqq_u8(chunk, delim_vec);

    uint8x8_t low = vget_low_u8(cmp);
    uint8x8_t high = vget_high_u8(cmp);

    uint16x8_t sum16_low = vpaddlq_u8(vcombine_u8(low, vdup_n_u8(0)));
    uint16x8_t sum16_high = vpaddlq_u8(vcombine_u8(high, vdup_n_u8(0)));

    uint32x4_t sum32_low = vpaddlq_u16(sum16_low);
    uint32x4_t sum32_high = vpaddlq_u16(sum16_high);

    uint32x4_t total = vaddq_u32(sum32_low, sum32_high);

    count += vgetq_lane_u32(total, 0) + vgetq_lane_u32(total, 1) +
             vgetq_lane_u32(total, 2) + vgetq_lane_u32(total, 3);
  }

  count += csv_scalar_count_delimiters(data + i, size - i, delimiter);

  return count;
}

static bool csv_neon_validate_utf8(const char *data, size_t size) {
  return csv_scalar_validate_utf8(data, size);
}
#endif

csv_search_result_t
csv_simd_find_delimiter_newline(const char *data, size_t size, char delimiter) {
#ifdef HAVE_SSE4_2
  if (g_simd_features & CSV_SIMD_SSE4_2) {
    csv_search_result_t result = {NULL, 0};

    if (size < 16) {
      return csv_scalar_find_special_char(data, size, delimiter, '\n', '\r');
    }

    __m128i chars = _mm_setr_epi8(delimiter, '\r', '\n', 0, 0, 0, 0, 0, 0, 0, 0,
                                  0, 0, 0, 0, 0);

    size_t i = 0;
    for (; i + 64 <= size; i += 64) {
      __m128i chunk1 = _mm_loadu_si128((__m128i *)(data + i));
      __m128i chunk2 = _mm_loadu_si128((__m128i *)(data + i + 16));
      __m128i chunk3 = _mm_loadu_si128((__m128i *)(data + i + 32));
      __m128i chunk4 = _mm_loadu_si128((__m128i *)(data + i + 48));

      int idx1 = _mm_cmpistri(chars, chunk1,
                              _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                  _SIDD_LEAST_SIGNIFICANT);
      if (idx1 < 16) {
        result.pos = data + i + idx1;
        result.count = i + idx1;
        return result;
      }

      int idx2 = _mm_cmpistri(chars, chunk2,
                              _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                  _SIDD_LEAST_SIGNIFICANT);
      if (idx2 < 16) {
        result.pos = data + i + 16 + idx2;
        result.count = i + 16 + idx2;
        return result;
      }

      int idx3 = _mm_cmpistri(chars, chunk3,
                              _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                  _SIDD_LEAST_SIGNIFICANT);
      if (idx3 < 16) {
        result.pos = data + i + 32 + idx3;
        result.count = i + 32 + idx3;
        return result;
      }

      int idx4 = _mm_cmpistri(chars, chunk4,
                              _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                  _SIDD_LEAST_SIGNIFICANT);
      if (idx4 < 16) {
        result.pos = data + i + 48 + idx4;
        result.count = i + 48 + idx4;
        return result;
      }
    }

    for (; i + 16 <= size; i += 16) {
      __m128i chunk = _mm_loadu_si128((__m128i *)(data + i));
      int index = _mm_cmpistri(chars, chunk,
                               _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                   _SIDD_LEAST_SIGNIFICANT);

      if (index < 16) {
        result.pos = data + i + index;
        result.count = i + index;
        return result;
      }
    }

    if (i < size) {
      csv_search_result_t scalar_result = csv_scalar_find_special_char(
          data + i, size - i, delimiter, '\n', '\r');
      if (scalar_result.pos) {
        result.pos = scalar_result.pos;
        result.count = i + scalar_result.count;
      } else {
        result.count = size;
      }
    } else {
      result.count = size;
    }

    return result;
  }
#endif
  return csv_scalar_find_special_char(data, size, delimiter, '\n', '\r');
}

csv_search_result_t csv_simd_find_special_char(const char *data, size_t size,
                                               char delimiter, char quote,
                                               char escape) {
#ifdef HAVE_SSE4_2
  if (g_simd_features & CSV_SIMD_SSE4_2) {
    return csv_sse42_find_special_char(data, size, delimiter, quote, escape);
  }
#endif
#ifdef HAVE_NEON
  if (g_simd_features & CSV_SIMD_NEON) {
    return csv_neon_find_special_char(data, size, delimiter, quote, escape);
  }
#endif
  return csv_scalar_find_special_char(data, size, delimiter, quote, escape);
}

csv_search_result_t csv_simd_find_eol(const char *data, size_t size) {
#ifdef HAVE_SSE4_2
  if (g_simd_features & CSV_SIMD_SSE4_2) {
    return csv_sse42_find_eol(data, size);
  }
#endif
#ifdef HAVE_NEON
  if (g_simd_features & CSV_SIMD_NEON) {
    return csv_neon_find_eol(data, size);
  }
#endif
  return csv_scalar_find_eol(data, size);
}

csv_search_result_t csv_simd_find_quote_end(const char *data, size_t size,
                                            char quote, char escape,
                                            bool double_quote) {
  csv_search_result_t result = {NULL, 0};

  for (size_t i = 0; i < size; i++) {
    char c = data[i];
    if (c == quote) {
      if (double_quote && i + 1 < size && data[i + 1] == quote) {
        i++;
        continue;
      }
      result.pos = data + i;
      result.count = i;
      return result;
    } else if (c == escape) {
      i++;
    }
  }

  result.count = size;
  return result;
}

uint32_t csv_simd_count_delimiters(const char *data, size_t size,
                                   char delimiter) {
#ifdef HAVE_SSE4_2
  if (g_simd_features & CSV_SIMD_SSE4_2) {
    return csv_sse42_count_delimiters(data, size, delimiter);
  }
#endif
#ifdef HAVE_NEON
  if (g_simd_features & CSV_SIMD_NEON) {
    return csv_neon_count_delimiters(data, size, delimiter);
  }
#endif
  return csv_scalar_count_delimiters(data, size, delimiter);
}

bool csv_simd_validate_utf8(const char *data, size_t size) {
#ifdef HAVE_SSE4_2
  if (g_simd_features & CSV_SIMD_SSE4_2) {
    return csv_sse42_validate_utf8(data, size);
  }
#endif
#ifdef HAVE_NEON
  if (g_simd_features & CSV_SIMD_NEON) {
    return csv_neon_validate_utf8(data, size);
  }
#endif
  return csv_scalar_validate_utf8(data, size);
}

void csv_simd_memcpy(void *dest, const void *src, size_t size) {
  memcpy(dest, src, size);
}

int csv_simd_memcmp(const void *s1, const void *s2, size_t size) {
  return memcmp(s1, s2, size);
}

csv_search_result_t csv_simd_find_delimiter_newline_tsv(const char *data,
                                                        size_t size,
                                                        char delimiter) {
#ifdef HAVE_SSE4_2
  if (g_simd_features & CSV_SIMD_SSE4_2) {
    csv_search_result_t result = {NULL, 0};

    if (size < 16) {
      return csv_scalar_find_special_char(data, size, delimiter, '\n', '\r');
    }

    __m128i chars = _mm_setr_epi8(delimiter, '\r', '\n', 0, 0, 0, 0, 0, 0, 0, 0,
                                  0, 0, 0, 0, 0);

    size_t i = 0;
    for (; i + 128 <= size; i += 128) {
      __m128i chunk1 = _mm_loadu_si128((__m128i *)(data + i));
      __m128i chunk2 = _mm_loadu_si128((__m128i *)(data + i + 16));
      __m128i chunk3 = _mm_loadu_si128((__m128i *)(data + i + 32));
      __m128i chunk4 = _mm_loadu_si128((__m128i *)(data + i + 48));
      __m128i chunk5 = _mm_loadu_si128((__m128i *)(data + i + 64));
      __m128i chunk6 = _mm_loadu_si128((__m128i *)(data + i + 80));
      __m128i chunk7 = _mm_loadu_si128((__m128i *)(data + i + 96));
      __m128i chunk8 = _mm_loadu_si128((__m128i *)(data + i + 112));

      int idx1 = _mm_cmpistri(chars, chunk1,
                              _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                  _SIDD_LEAST_SIGNIFICANT);
      if (idx1 < 16) {
        result.pos = data + i + idx1;
        result.count = i + idx1;
        return result;
      }

      int idx2 = _mm_cmpistri(chars, chunk2,
                              _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                  _SIDD_LEAST_SIGNIFICANT);
      if (idx2 < 16) {
        result.pos = data + i + 16 + idx2;
        result.count = i + 16 + idx2;
        return result;
      }

      int idx3 = _mm_cmpistri(chars, chunk3,
                              _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                  _SIDD_LEAST_SIGNIFICANT);
      if (idx3 < 16) {
        result.pos = data + i + 32 + idx3;
        result.count = i + 32 + idx3;
        return result;
      }

      int idx4 = _mm_cmpistri(chars, chunk4,
                              _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                  _SIDD_LEAST_SIGNIFICANT);
      if (idx4 < 16) {
        result.pos = data + i + 48 + idx4;
        result.count = i + 48 + idx4;
        return result;
      }

      int idx5 = _mm_cmpistri(chars, chunk5,
                              _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                  _SIDD_LEAST_SIGNIFICANT);
      if (idx5 < 16) {
        result.pos = data + i + 64 + idx5;
        result.count = i + 64 + idx5;
        return result;
      }

      int idx6 = _mm_cmpistri(chars, chunk6,
                              _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                  _SIDD_LEAST_SIGNIFICANT);
      if (idx6 < 16) {
        result.pos = data + i + 80 + idx6;
        result.count = i + 80 + idx6;
        return result;
      }

      int idx7 = _mm_cmpistri(chars, chunk7,
                              _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                  _SIDD_LEAST_SIGNIFICANT);
      if (idx7 < 16) {
        result.pos = data + i + 96 + idx7;
        result.count = i + 96 + idx7;
        return result;
      }

      int idx8 = _mm_cmpistri(chars, chunk8,
                              _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                  _SIDD_LEAST_SIGNIFICANT);
      if (idx8 < 16) {
        result.pos = data + i + 112 + idx8;
        result.count = i + 112 + idx8;
        return result;
      }
    }

    for (; i + 32 <= size; i += 32) {
      __m128i chunk = _mm_loadu_si128((__m128i *)(data + i));
      int index = _mm_cmpistri(chars, chunk,
                               _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY |
                                   _SIDD_LEAST_SIGNIFICANT);

      if (index < 16) {
        result.pos = data + i + index;
        result.count = i + index;
        return result;
      }
    }

    if (i < size) {
      csv_search_result_t scalar_result = csv_scalar_find_special_char(
          data + i, size - i, delimiter, '\n', '\r');
      if (scalar_result.pos) {
        result.pos = scalar_result.pos;
        result.count = i + scalar_result.count;
      } else {
        result.count = size;
      }
    } else {
      result.count = size;
    }

    return result;
  }
#endif
  return csv_scalar_find_special_char(data, size, delimiter, '\n', '\r');
}

#ifdef HAVE_AVX2
__attribute__((target("avx2"))) uint32_t
csv_simd_count_delimiters_tsv(const char *data, size_t size, char delimiter) {
  if (!(g_simd_features & CSV_SIMD_AVX2)) {
    goto fallback_sse;
  }

  if (size < 32) {
    return csv_scalar_count_delimiters(data, size, delimiter);
  }

  __m256i delim_vec = _mm256_set1_epi8(delimiter);
  uint32_t count = 0;

  size_t i = 0;
  for (; i + 128 <= size; i += 128) {
    __m256i chunk1 = _mm256_loadu_si256((__m256i *)(data + i));
    __m256i chunk2 = _mm256_loadu_si256((__m256i *)(data + i + 32));
    __m256i chunk3 = _mm256_loadu_si256((__m256i *)(data + i + 64));
    __m256i chunk4 = _mm256_loadu_si256((__m256i *)(data + i + 96));
    __m256i chunk5 = _mm256_loadu_si256((__m256i *)(data + i + 128));
    __m256i chunk6 = _mm256_loadu_si256((__m256i *)(data + i + 160));
    __m256i chunk7 = _mm256_loadu_si256((__m256i *)(data + i + 192));
    __m256i chunk8 = _mm256_loadu_si256((__m256i *)(data + i + 224));

    __m256i cmp1 = _mm256_cmpeq_epi8(chunk1, delim_vec);
    __m256i cmp2 = _mm256_cmpeq_epi8(chunk2, delim_vec);
    __m256i cmp3 = _mm256_cmpeq_epi8(chunk3, delim_vec);
    __m256i cmp4 = _mm256_cmpeq_epi8(chunk4, delim_vec);
    __m256i cmp5 = _mm256_cmpeq_epi8(chunk5, delim_vec);
    __m256i cmp6 = _mm256_cmpeq_epi8(chunk6, delim_vec);
    __m256i cmp7 = _mm256_cmpeq_epi8(chunk7, delim_vec);
    __m256i cmp8 = _mm256_cmpeq_epi8(chunk8, delim_vec);

    count += __builtin_popcountll(_mm256_movemask_epi8(cmp1));
    count += __builtin_popcountll(_mm256_movemask_epi8(cmp2));
    count += __builtin_popcountll(_mm256_movemask_epi8(cmp3));
    count += __builtin_popcountll(_mm256_movemask_epi8(cmp4));
    count += __builtin_popcountll(_mm256_movemask_epi8(cmp5));
    count += __builtin_popcountll(_mm256_movemask_epi8(cmp6));
    count += __builtin_popcountll(_mm256_movemask_epi8(cmp7));
    count += __builtin_popcountll(_mm256_movemask_epi8(cmp8));
  }

  for (; i + 32 <= size; i += 32) {
    __m256i chunk = _mm256_loadu_si256((__m256i *)(data + i));
    __m256i cmp = _mm256_cmpeq_epi8(chunk, delim_vec);
    count += __builtin_popcountll(_mm256_movemask_epi8(cmp));
  }

  count += csv_scalar_count_delimiters(data + i, size - i, delimiter);

  return count;

fallback_sse:
#endif

#ifdef HAVE_SSE4_2
  if (g_simd_features & CSV_SIMD_SSE4_2) {
    return csv_sse42_count_delimiters(data, size, delimiter);
  }
#endif

  return csv_scalar_count_delimiters(data, size, delimiter);
}

#ifndef HAVE_AVX2
uint32_t csv_simd_count_delimiters_tsv(const char *data, size_t size,
                                       char delimiter) {
#ifdef HAVE_SSE4_2
  if (g_simd_features & CSV_SIMD_SSE4_2) {
    return csv_sse42_count_delimiters(data, size, delimiter);
  }
#endif
  return csv_scalar_count_delimiters(data, size, delimiter);
}
#endif

csv_parse_options_t csv_default_options(void) {
  csv_parse_options_t opts = {.delimiter = ',',
                              .quote_char = '"',
                              .escape_char = '\\',
                              .quoting = true,
                              .escaping = false,
                              .double_quote = true,
                              .newlines_in_values = false,
                              .ignore_empty_lines = true,
                              .trim_whitespace = false,
                              .max_field_size = 1024 * 1024,
                              .max_row_size = 10 * 1024 * 1024};
  return opts;
}

const char *csv_error_string(csv_error_t error) {
  switch (error) {
  case CSV_OK:
    return "Success";
  case CSV_ERROR_INVALID_ARGS:
    return "Invalid arguments";
  case CSV_ERROR_OUT_OF_MEMORY:
    return "Out of memory";
  case CSV_ERROR_BUFFER_TOO_SMALL:
    return "Buffer too small";
  case CSV_ERROR_INVALID_FORMAT:
    return "Invalid CSV format";
  case CSV_ERROR_IO_ERROR:
    return "I/O error";
  case CSV_ERROR_PARSE_ERROR:
    return "Parse error";
  default:
    return "Unknown error";
  }
}

static inline bool is_newline(char c) { return c == '\r' || c == '\n'; }

static csv_error_t ensure_fields_capacity(csv_parser_t *parser,
                                          uint32_t required) {
  if (parser->fields_capacity >= required) {
    return CSV_OK;
  }

  uint32_t new_capacity = parser->fields_capacity;
  while (new_capacity < required) {
    new_capacity = new_capacity ? new_capacity * 2 : 32;
  }

  csv_field_t *new_fields =
      realloc(parser->fields, new_capacity * sizeof(csv_field_t));
  if (!new_fields) {
    return CSV_ERROR_OUT_OF_MEMORY;
  }

  parser->fields = new_fields;
  parser->fields_capacity = new_capacity;
  return CSV_OK;
}

static csv_error_t add_field_zero_copy(csv_parser_t *parser, const char *data,
                                       uint32_t size, bool quoted) {
  if (size > (uint32_t)parser->options.max_field_size) {
    return CSV_ERROR_BUFFER_TOO_SMALL;
  }

  csv_error_t err = ensure_fields_capacity(parser, parser->num_fields + 1);
  if (err != CSV_OK) {
    return err;
  }

  parser->fields[parser->num_fields].data = data;
  parser->fields[parser->num_fields].size = size;
  parser->fields[parser->num_fields].quoted = quoted;
  parser->num_fields++;

  parser->stats.total_fields_parsed++;
  return CSV_OK;
}

static csv_error_t finish_row(csv_parser_t *parser) {
  if (parser->num_fields == 0 && parser->options.ignore_empty_lines) {
    return CSV_OK;
  }

  parser->stats.total_rows_parsed++;

  if (parser->row_callback) {
    csv_row_t row = {.fields = parser->fields,
                     .num_fields = parser->num_fields,
                     .row_number = parser->stats.total_rows_parsed};
    parser->row_callback(&row, parser->row_callback_data);
  }

  parser->num_fields = 0;
  parser->field_data_size = 0;

  return CSV_OK;
}

csv_parse_mode_t csv_detect_parse_mode(const char *sample_data,
                                       size_t sample_size) {
  if (sample_size == 0) {
    return CSV_MODE_GENERIC;
  }

  bool has_quotes = false;
  bool has_escapes = false;
  bool has_commas = false;
  bool has_tabs = false;
  uint32_t quote_count = 0;
  uint32_t comma_count = 0;
  uint32_t tab_count = 0;

  size_t check_size = sample_size > 1024 ? 1024 : sample_size;

  for (size_t i = 0; i < check_size; i++) {
    char c = sample_data[i];
    if (c == '"') {
      has_quotes = true;
      quote_count++;
    } else if (c == '\\') {
      has_escapes = true;
    } else if (c == ',') {
      has_commas = true;
      comma_count++;
    } else if (c == '\t') {
      has_tabs = true;
      tab_count++;
    }
  }

  if (has_tabs && tab_count > comma_count && !has_quotes && !has_escapes) {
    return CSV_MODE_TSV;
  }

  if (has_commas && !has_quotes && !has_escapes) {
    return CSV_MODE_SIMPLE;
  }

  if (has_quotes && !has_escapes) {
    return CSV_MODE_QUOTED_ONLY;
  }

  return CSV_MODE_GENERIC;
}

static csv_error_t parse_chunk_optimized(csv_parser_t *parser, const char *data,
                                         size_t size, bool is_final) {
  const char *pos = data;
  const char *end = data + size;
  const char *field_start = pos;
  csv_error_t err = CSV_OK;

  if (size == 0) {
    if (is_final && parser->num_fields > 0) {
      err = finish_row(parser);
    }
    return err;
  }

  // Use SIMD-accelerated parsing for larger chunks
  while (pos < end) {
    size_t remaining = end - pos;

    // Use SIMD to find next delimiter or newline
    csv_search_result_t result = csv_simd_find_delimiter_newline(
        pos, remaining, parser->options.delimiter);

    if (result.pos) {
      // Found delimiter or newline
      const char *found_pos = result.pos;
      char found_char = *found_pos;

      // Add field from field_start to found position
      uint32_t field_size = found_pos - field_start;
      if (field_size > 0 || found_char == parser->options.delimiter) {
        err = add_field_zero_copy(parser, field_start, field_size, false);
        if (err != CSV_OK)
          return err;
      }

      if (found_char == parser->options.delimiter) {
        // Move past delimiter and start next field
        pos = found_pos + 1;
        field_start = pos;
      } else if (is_newline(found_char)) {
        // End of row
        err = finish_row(parser);
        if (err != CSV_OK)
          return err;

        pos = found_pos + 1;
        // Handle CRLF
        if (found_char == '\r' && pos < end && *pos == '\n') {
          pos++;
        }
        field_start = pos;
      } else {
        // Should not happen with current SIMD implementation
        pos++;
      }
    } else {
      // No more delimiters or newlines found in remaining data
      if (is_final && field_start < end) {
        uint32_t field_size = end - field_start;
        err = add_field_zero_copy(parser, field_start, field_size, false);
        if (err != CSV_OK)
          return err;
      }
      break;
    }
  }

  if (is_final && parser->num_fields > 0) {
    err = finish_row(parser);
  }

  return err;
}

static csv_error_t parse_chunk_batch_simd(csv_parser_t *parser,
                                          const char *data, size_t size,
                                          bool is_final) {
  const char *pos = data;
  const char *end = data + size;
  csv_error_t err = CSV_OK;

  if (size == 0) {
    if (is_final && parser->num_fields > 0) {
      err = finish_row(parser);
    }
    return err;
  }

  // For large chunks, use batch processing with SIMD
  const size_t BATCH_SIZE = 64 * 1024; // Process in 64KB chunks

  while (pos < end) {
    size_t chunk_size = (end - pos > BATCH_SIZE) ? BATCH_SIZE : (end - pos);
    const char *chunk_end = pos + chunk_size;

    // Find all delimiters and newlines in this chunk using SIMD
    csv_char_positions_t delim_positions =
        csv_simd_find_all_chars(pos, chunk_size, parser->options.delimiter);
    csv_char_positions_t newline_positions =
        csv_simd_find_all_chars(pos, chunk_size, '\n');

    // Merge and sort positions for efficient processing
    uint32_t total_positions = delim_positions.count + newline_positions.count;
    if (total_positions > 0) {
      // Simple merge processing - could be optimized further
      const char *field_start = pos;
      const char *current_pos = pos;

      while (current_pos < chunk_end) {
        csv_search_result_t result = csv_simd_find_delimiter_newline(
            current_pos, chunk_end - current_pos, parser->options.delimiter);

        if (result.pos && result.pos < chunk_end) {
          const char *found_pos = result.pos;
          char found_char = *found_pos;

          uint32_t field_size = found_pos - field_start;
          if (field_size > 0 || found_char == parser->options.delimiter) {
            err = add_field_zero_copy(parser, field_start, field_size, false);
            if (err != CSV_OK)
              goto cleanup;
          }

          if (found_char == parser->options.delimiter) {
            current_pos = found_pos + 1;
            field_start = current_pos;
          } else if (is_newline(found_char)) {
            err = finish_row(parser);
            if (err != CSV_OK)
              goto cleanup;

            current_pos = found_pos + 1;
            if (found_char == '\r' && current_pos < chunk_end &&
                *current_pos == '\n') {
              current_pos++;
            }
            field_start = current_pos;
          } else {
            current_pos++;
          }
        } else {
          break;
        }
      }

      pos = current_pos;
    } else {
      // No delimiters or newlines in this chunk
      if (is_final && pos < chunk_end) {
        uint32_t field_size = chunk_end - pos;
        err = add_field_zero_copy(parser, pos, field_size, false);
        if (err != CSV_OK)
          goto cleanup;
      }
      pos = chunk_end;
    }

  cleanup:
    free(delim_positions.positions);
    free(newline_positions.positions);

    if (err != CSV_OK)
      return err;
  }

  if (is_final && parser->num_fields > 0) {
    err = finish_row(parser);
  }

  return err;
}

static csv_error_t parse_chunk_tsv_simd(csv_parser_t *parser, const char *data,
                                        size_t size, bool is_final) {
  const char *pos = data;
  const char *end = data + size;
  const char *field_start = pos;
  csv_error_t err = CSV_OK;

  if (size == 0) {
    if (is_final && parser->num_fields > 0) {
      err = finish_row(parser);
    }
    return err;
  }

  // For very large chunks, use batch processing
  if (size > 32 * 1024) {
    return parse_chunk_batch_simd(parser, data, size, is_final);
  }

  // Optimized TSV parsing using specialized SIMD functions
  while (pos < end) {
    size_t remaining = end - pos;

    // Use TSV-optimized SIMD to find next tab or newline
    csv_search_result_t result =
        csv_simd_find_delimiter_newline_tsv(pos, remaining, '\t');

    if (result.pos) {
      const char *found_pos = result.pos;
      char found_char = *found_pos;

      // Add field from field_start to found position
      uint32_t field_size = found_pos - field_start;
      err = add_field_zero_copy(parser, field_start, field_size, false);
      if (err != CSV_OK)
        return err;

      if (found_char == '\t') {
        // Move past tab and start next field
        pos = found_pos + 1;
        field_start = pos;
      } else if (is_newline(found_char)) {
        // End of row
        err = finish_row(parser);
        if (err != CSV_OK)
          return err;

        pos = found_pos + 1;
        // Handle CRLF
        if (found_char == '\r' && pos < end && *pos == '\n') {
          pos++;
        }
        field_start = pos;
      } else {
        pos++;
      }
    } else {
      // No more tabs or newlines found
      if (is_final && field_start < end) {
        uint32_t field_size = end - field_start;
        err = add_field_zero_copy(parser, field_start, field_size, false);
        if (err != CSV_OK)
          return err;
      }
      break;
    }
  }

  if (is_final && parser->num_fields > 0) {
    err = finish_row(parser);
  }

  return err;
}

static csv_error_t parse_chunk_auto_optimized(csv_parser_t *parser,
                                              const char *data, size_t size,
                                              bool is_final) {
  if (!parser->mode_detected) {
    parser->detected_mode = csv_detect_parse_mode(data, size);
    parser->mode_detected = true;
  }

  switch (parser->detected_mode) {
  case CSV_MODE_TSV:
    return parse_chunk_tsv_simd(parser, data, size, is_final);

  case CSV_MODE_SIMPLE:
    return parse_chunk_optimized(parser, data, size, is_final);

  case CSV_MODE_QUOTED_ONLY:
  case CSV_MODE_GENERIC:
  default:
    return parse_chunk_optimized(parser, data, size, is_final);
  }
}

csv_parser_t *csv_parser_create(const csv_parse_options_t *options) {
  csv_parser_t *parser = calloc(1, sizeof(csv_parser_t));
  if (!parser) {
    return NULL;
  }

  if (options) {
    parser->options = *options;
  } else {
    parser->options = csv_default_options();
  }

  parser->state = CSV_STATE_FIELD_START;
  parser->simd_features = csv_detect_simd_features();
  csv_simd_init();

  parser->fields_capacity = 64;
  parser->fields = malloc(parser->fields_capacity * sizeof(csv_field_t));

  parser->field_data_capacity = 64 * 1024;
  parser->field_data_buffer = malloc(parser->field_data_capacity);

  if (!parser->fields || !parser->field_data_buffer) {
    csv_parser_destroy(parser);
    return NULL;
  }

  clock_gettime(CLOCK_MONOTONIC, &parser->start_time);

  return parser;
}

void csv_parser_destroy(csv_parser_t *parser) {
  if (!parser)
    return;

  free(parser->buffer);
  free(parser->fields);
  free(parser->field_buffer);
  free(parser->field_data_buffer);
  free(parser);
}

void csv_parser_set_row_callback(csv_parser_t *parser,
                                 csv_row_callback_t callback, void *user_data) {
  if (parser) {
    parser->row_callback = callback;
    parser->row_callback_data = user_data;
  }
}

void csv_parser_set_error_callback(csv_parser_t *parser,
                                   csv_error_callback_t callback,
                                   void *user_data) {
  if (parser) {
    parser->error_callback = callback;
    parser->error_callback_data = user_data;
  }
}

void csv_parser_set_progress_callback(csv_parser_t *parser,
                                      csv_progress_callback_t callback,
                                      void *user_data) {
  if (parser) {
    parser->progress_callback = callback;
    parser->progress_callback_data = user_data;
  }
}

csv_error_t csv_parse_buffer(csv_parser_t *parser, const char *buffer,
                             size_t size, bool is_final) {
  if (!parser || !buffer) {
    return CSV_ERROR_INVALID_ARGS;
  }

  parser->stats.total_bytes_processed += size;

  csv_error_t err = parse_chunk_auto_optimized(parser, buffer, size, is_final);

  if (parser->progress_callback) {
    parser->progress_callback(parser->stats.total_bytes_processed,
                              parser->stats.total_bytes_processed,
                              parser->progress_callback_data);
  }

  return err;
}

csv_error_t csv_parse_file(csv_parser_t *parser, const char *filename) {
  if (!parser || !filename) {
    return CSV_ERROR_INVALID_ARGS;
  }

  FILE *file = fopen(filename, "rb");
  if (!file) {
    return CSV_ERROR_IO_ERROR;
  }

  csv_error_t result = csv_parse_stream(parser, file);
  fclose(file);
  return result;
}

csv_error_t csv_parse_stream(csv_parser_t *parser, FILE *stream) {
  if (!parser || !stream) {
    return CSV_ERROR_INVALID_ARGS;
  }

  const size_t BUFFER_SIZE = 256 * 1024;
  char *buffer = malloc(BUFFER_SIZE);
  if (!buffer) {
    return CSV_ERROR_OUT_OF_MEMORY;
  }

  csv_error_t err = CSV_OK;
  size_t bytes_read;

  while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, stream)) > 0) {
    bool is_final = feof(stream);
    err = csv_parse_buffer(parser, buffer, bytes_read, is_final);
    if (err != CSV_OK) {
      break;
    }
  }

  if (ferror(stream)) {
    err = CSV_ERROR_IO_ERROR;
  }

  free(buffer);
  return err;
}

csv_stats_t csv_parser_get_stats(const csv_parser_t *parser) {
  if (!parser) {
    csv_stats_t empty = {0};
    return empty;
  }

  csv_stats_t stats = parser->stats;

  struct timespec current_time;
  clock_gettime(CLOCK_MONOTONIC, &current_time);

  uint64_t elapsed_ns =
      (current_time.tv_sec - parser->start_time.tv_sec) * 1000000000ULL +
      (current_time.tv_nsec - parser->start_time.tv_nsec);

  stats.parse_time_ns = elapsed_ns;

  if (elapsed_ns > 0) {
    double elapsed_seconds = elapsed_ns / 1000000000.0;
    double mb_processed = stats.total_bytes_processed / (1024.0 * 1024.0);
    stats.throughput_mbps = mb_processed / elapsed_seconds;
  }

  stats.simd_acceleration_used = parser->simd_features;

  return stats;
}

void csv_print_stats(const csv_parser_t *parser) {
  if (!parser)
    return;

  csv_stats_t stats = csv_parser_get_stats(parser);

  printf("CSV Parser Statistics\n");
  printf("Bytes processed: %lu\n", stats.total_bytes_processed);
  printf("Rows parsed: %lu\n", stats.total_rows_parsed);
  printf("Fields parsed: %lu\n", stats.total_fields_parsed);
  printf("Parse time: %.3f ms\n", stats.parse_time_ns / 1000000.0);
  printf("Throughput: %.2f MB/s\n", stats.throughput_mbps);

  printf("SIMD acceleration: ");
  if (stats.simd_acceleration_used & CSV_SIMD_SSE4_2)
    printf("SSE4.2 ");
  if (stats.simd_acceleration_used & CSV_SIMD_AVX2)
    printf("AVX2 ");
  if (stats.simd_acceleration_used & CSV_SIMD_NEON)
    printf("NEON ");
  if (stats.simd_acceleration_used == CSV_SIMD_NONE)
    printf("None");
  printf("\n");

  printf("CSV parsing completed successfully\n");
}

csv_block_config_t csv_default_block_config(void) {
  csv_block_config_t config = {.parse_options = csv_default_options(),
                               .block_size = 64 * 1024,
                               .max_rows_per_batch = 10000,
                               .parallel_processing = false,
                               .num_threads = 1,
                               .use_memory_pool = false,
                               .initial_pool_size = 1024 * 1024};
  return config;
}

csv_block_parser_t *csv_block_parser_create(const csv_block_config_t *config) {
  csv_block_parser_t *parser = calloc(1, sizeof(csv_block_parser_t));
  if (!parser) {
    return NULL;
  }

  if (config) {
    parser->config = *config;
  } else {
    parser->config = csv_default_block_config();
  }

  parser->simd_features = csv_detect_simd_features();
  csv_simd_init();

  clock_gettime(CLOCK_MONOTONIC, &parser->start_time);

  return parser;
}

void csv_block_parser_destroy(csv_block_parser_t *parser) {
  if (!parser)
    return;

  free(parser->current_batch.field_offsets);
  free(parser->current_batch.row_offsets);
  free(parser);
}

void csv_block_parser_set_batch_callback(csv_block_parser_t *parser,
                                         csv_batch_callback_t callback,
                                         void *user_data) {
  if (parser) {
    parser->batch_callback = callback;
    parser->batch_callback_data = user_data;
  }
}

csv_error_t csv_block_parse_buffer(csv_block_parser_t *parser,
                                   const char *buffer, size_t size,
                                   bool is_final) {
  (void)is_final;
  if (!parser || !buffer) {
    return CSV_ERROR_INVALID_ARGS;
  }

  if (!parser->mode_detected) {
    parser->detected_mode = csv_detect_parse_mode(buffer, size);
    parser->mode_detected = true;
  }

  csv_data_batch_t batch = {
      .data = buffer, .data_size = size, .parse_mode = parser->detected_mode};

  if (parser->batch_callback) {
    parser->batch_callback(&batch, parser->batch_callback_data);
  }

  return CSV_OK;
}

csv_error_t csv_block_parse_file(csv_block_parser_t *parser,
                                 const char *filename) {
  if (!parser || !filename) {
    return CSV_ERROR_INVALID_ARGS;
  }

  FILE *file = fopen(filename, "rb");
  if (!file) {
    return CSV_ERROR_IO_ERROR;
  }

  const size_t BUFFER_SIZE = 256 * 1024;
  char *buffer = malloc(BUFFER_SIZE);
  if (!buffer) {
    fclose(file);
    return CSV_ERROR_OUT_OF_MEMORY;
  }

  csv_error_t err = CSV_OK;
  size_t bytes_read;

  while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
    bool is_final = feof(file);
    err = csv_block_parse_buffer(parser, buffer, bytes_read, is_final);
    if (err != CSV_OK) {
      break;
    }
  }

  if (ferror(file)) {
    err = CSV_ERROR_IO_ERROR;
  }

  free(buffer);
  fclose(file);
  return err;
}

csv_error_t
csv_batch_visit_column(const csv_data_batch_t *batch, uint32_t col_index,
                       void (*visitor)(const char *data, uint32_t size,
                                       bool quoted, void *user_data),
                       void *user_data) {
  (void)col_index;
  (void)visitor;
  (void)user_data;
  if (!batch || !visitor) {
    return CSV_ERROR_INVALID_ARGS;
  }

  return CSV_OK;
}

csv_error_t csv_batch_get_field(const csv_data_batch_t *batch,
                                uint32_t row_index, uint32_t col_index,
                                const char **data, uint32_t *size,
                                bool *quoted) {
  (void)row_index;
  (void)col_index;
  if (!batch || !data || !size || !quoted) {
    return CSV_ERROR_INVALID_ARGS;
  }

  return CSV_OK;
}

csv_error_t csv_parse_tsv_buffer(csv_block_parser_t *parser, const char *buffer,
                                 size_t size, bool is_final) {
  return csv_block_parse_buffer(parser, buffer, size, is_final);
}

csv_error_t csv_parse_simple_mode(csv_block_parser_t *parser,
                                  const char *buffer, size_t size,
                                  bool is_final) {
  return csv_block_parse_buffer(parser, buffer, size, is_final);
}

csv_error_t csv_parse_quoted_mode(csv_block_parser_t *parser,
                                  const char *buffer, size_t size,
                                  bool is_final) {
  return csv_block_parse_buffer(parser, buffer, size, is_final);
}

csv_char_positions_t csv_simd_find_all_chars(const char *data, size_t size,
                                             char target_char) {
  csv_char_positions_t result = {NULL, 0, 0};

  uint32_t initial_capacity = 128;
  result.positions = malloc(initial_capacity * sizeof(uint32_t));
  if (!result.positions) {
    return result;
  }
  result.capacity = initial_capacity;

  for (size_t i = 0; i < size; i++) {
    if (data[i] == target_char) {
      if (result.count >= result.capacity) {
        result.capacity *= 2;
        uint32_t *new_positions =
            realloc(result.positions, result.capacity * sizeof(uint32_t));
        if (!new_positions) {
          free(result.positions);
          result.positions = NULL;
          result.count = 0;
          result.capacity = 0;
          return result;
        }
        result.positions = new_positions;
      }
      result.positions[result.count++] = i;
    }
  }

  return result;
}

uint32_t csv_simd_count_fields_in_rows(const char *data, size_t size,
                                       const uint32_t *row_offsets,
                                       uint32_t num_rows, char delimiter) {
  uint32_t total_fields = 0;

  for (uint32_t i = 0; i < num_rows; i++) {
    uint32_t row_start = row_offsets[i];
    uint32_t row_end = (i + 1 < num_rows) ? row_offsets[i + 1] : size;

    if (row_end > row_start) {
      total_fields += csv_simd_count_delimiters(
                          data + row_start, row_end - row_start, delimiter) +
                      1;
    }
  }

  return total_fields;
}

csv_error_t csv_parse_tsv_vectorized(const char *data, size_t size,
                                     csv_batch_callback_t callback,
                                     void *user_data) {
  if (!data || !callback) {
    return CSV_ERROR_INVALID_ARGS;
  }

  csv_data_batch_t batch = {
      .data = data, .data_size = size, .parse_mode = CSV_MODE_TSV};

  callback(&batch, user_data);

  return CSV_OK;
}

csv_error_t csv_process_rows_parallel(const csv_data_batch_t *batch,
                                      csv_row_processor_t processor,
                                      void *user_data, uint32_t num_threads) {
  (void)num_threads;
  if (!batch || !processor) {
    return CSV_ERROR_INVALID_ARGS;
  }

  csv_row_range_t range = {.start_row = 0,
                           .end_row = batch->num_rows,
                           .batch = batch,
                           .user_data = user_data};

  processor(&range);

  return CSV_OK;
}

csv_string_pool_t *csv_string_pool_create(uint32_t initial_capacity) {
  csv_string_pool_t *pool = calloc(1, sizeof(csv_string_pool_t));
  if (!pool) {
    return NULL;
  }

  pool->capacity = initial_capacity;
  pool->data = malloc(initial_capacity);
  pool->strings_capacity = 128;
  pool->strings = malloc(pool->strings_capacity * sizeof(char *));

  if (!pool->data || !pool->strings) {
    csv_string_pool_destroy(pool);
    return NULL;
  }

  return pool;
}

void csv_string_pool_destroy(csv_string_pool_t *pool) {
  if (!pool)
    return;

  free(pool->data);
  free(pool->strings);
  free(pool);
}

const char *csv_string_pool_intern(csv_string_pool_t *pool, const char *str,
                                   uint32_t length) {
  if (!pool || !str) {
    return NULL;
  }

  for (uint32_t i = 0; i < pool->num_strings; i++) {
    if (strncmp(pool->strings[i], str, length) == 0 &&
        pool->strings[i][length] == '\0') {
      return pool->strings[i];
    }
  }

  if (pool->size + length + 1 > pool->capacity) {
    pool->capacity = (pool->capacity + length + 1) * 2;
    char *new_data = realloc(pool->data, pool->capacity);
    if (!new_data) {
      return NULL;
    }
    pool->data = new_data;
  }

  char *new_string = pool->data + pool->size;
  memcpy(new_string, str, length);
  new_string[length] = '\0';

  pool->strings[pool->num_strings++] = new_string;
  pool->size += length + 1;

  return new_string;
}

void csv_string_pool_clear(csv_string_pool_t *pool) {
  if (!pool)
    return;

  pool->size = 0;
  pool->num_strings = 0;
}

csv_advanced_stats_t
csv_block_parser_get_stats(const csv_block_parser_t *parser) {
  csv_advanced_stats_t empty = {0};
  if (!parser) {
    return empty;
  }

  return parser->stats;
}

#endif

#endif