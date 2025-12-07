# SonicSV

[![Version](https://img.shields.io/badge/version-3.1.1-blue.svg)](https://github.com/vitruves/SonicSV)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](https://github.com/vitruves/SonicSV/blob/main/LICENSE)
[![C99](https://img.shields.io/badge/standard-C99-blue.svg)](https://en.wikipedia.org/wiki/C99)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)](https://github.com/vitruves/SonicSV)

A fast, single-header CSV parser for C with automatic SIMD acceleration.

---

## Table of Contents

- [Why SonicSV?](#why-sonicsv)
- [Features](#features)
- [Quick Start](#quick-start)
- [Performance](#performance)
- [API Reference](#api-reference)
- [Installation](#installation)
- [Advanced Usage](#advanced-usage)
- [Supported Platforms](#supported-platforms)
- [Thread Safety](#thread-safety)
- [Contributing](#contributing)

---

## Why SonicSV?

SonicSV is a header-only CSV parser with SIMD acceleration for improved performance on modern CPUs. It provides 2-15x speedup on simple CSV data compared to libcsv, with smaller gains on complex quoted fields.

**What it does well:**

- Large files with simple structure (logs, exports, analytics data)
- Applications where CSV parsing is a measurable bottleneck
- Projects that want zero build dependencies
- Streaming large datasets with memory constraints

## Features

- **Single header** - Copy `sonicsv.h` into your project, no build dependencies
- **SIMD acceleration** - Runtime detection and dispatch for SSE4.2, AVX2, AVX-512, NEON, SVE
- **Streaming parser** - Callback-based API processes files without loading into memory
- **Memory controls** - Configurable limits on field size, row size, and total memory usage
- **Thread-safe** - Each parser instance is independent and can be used concurrently
- **Flexible parsing** - Custom delimiters, quote characters, whitespace trimming
- **Error handling** - Detailed error codes with row/column tracking, strict and lenient modes
- **Statistics** - Built-in performance counters and memory usage tracking
- **RFC 4180 compliant** - Handles quoted fields, escaped quotes, CRLF line endings
- **Cross-platform** - Works on x86_64, ARM64, and other architectures with optimized fallbacks

## Quick Start

**1. Copy `sonicsv.h` into your project**

**2. Use it:**

```c
#define SONICSV_IMPLEMENTATION
#include "sonicsv.h"

void on_row(const csv_row_t *row, void *ctx) {
    for (size_t i = 0; i < row->num_fields; i++) {
        const csv_field_t *f = csv_get_field(row, i);
        printf("%.*s ", (int)f->size, f->data);
    }
    printf("\n");
}

int main() {
    csv_parser_t *p = csv_parser_create(NULL);
    csv_parser_set_row_callback(p, on_row, NULL);
    csv_parse_file(p, "data.csv");
    csv_parser_destroy(p);
    return 0;
}
```

**3. Compile:**

```bash
gcc -O3 -march=native your_program.c -o your_program
```

## Performance

Benchmarks on MacBook Air M3 (ARM64/NEON), parsing ~230 MB of CSV data:

| Test Case              | SonicSV  | libcsv   | Speedup        |
| ---------------------- | -------- | -------- | -------------- |
| Simple CSV (no quotes) | 1.9 GB/s | 320 MB/s | **6x**   |
| Long fields            | 3.7 GB/s | 355 MB/s | **10x**  |
| Very long fields       | 5.5 GB/s | 364 MB/s | **15x**  |
| Quoted with commas     | 494 MB/s | 331 MB/s | **1.5x** |
| Quoted with newlines   | 980 MB/s | 360 MB/s | **2.7x** |

**Aggregate: 5.6x faster** (230 MB in 0.12s vs 0.69s)

The speedup is highest for simple, unquoted CSV data. Complex quoted fields with embedded delimiters see smaller gains.

### Comparison with libcsv

| Aspect                           | SonicSV                  | libcsv             |
| -------------------------------- | ------------------------ | ------------------ |
| **Simple CSV performance** | 1.9-5.5 GB/s             | 320-364 MB/s       |
| **Quoted CSV performance** | 494-980 MB/s             | 331-360 MB/s       |
| **Integration**            | Single header file       | Library dependency |
| **Memory usage**           | Configurable limits      | Fixed per design   |
| **API style**              | Callback-based streaming | Callback-based     |
| **SIMD support**           | Runtime detection        | No                 |
| **Strict RFC 4180**        | Optional                 | Yes                |
| **Code size**              | ~1850 lines              | ~1000 lines        |

## API Reference

### Parser Lifecycle

```c
// Create parser with default or custom options
csv_parser_t *csv_parser_create(const csv_parse_options_t *options);

// Reset parser state for reuse (avoids reallocation)
csv_error_t csv_parser_reset(csv_parser_t *parser);

// Free all resources
void csv_parser_destroy(csv_parser_t *parser);
```

### Parsing Functions

```c
// Parse entire file (uses mmap for zero-copy when available)
csv_error_t csv_parse_file(csv_parser_t *parser, const char *filename);

// Parse from FILE* stream
csv_error_t csv_parse_stream(csv_parser_t *parser, FILE *stream);

// Parse null-terminated string
csv_error_t csv_parse_string(csv_parser_t *parser, const char *csv_string);

// Parse raw buffer (set is_final=true for last chunk)
csv_error_t csv_parse_buffer(csv_parser_t *parser, const char *buffer,
                               size_t size, bool is_final);
```

### Callbacks

```c
// Row callback - invoked for each parsed row
typedef void (*csv_row_callback_t)(const csv_row_t *row, void *user_data);
void csv_parser_set_row_callback(csv_parser_t *parser,
                                   csv_row_callback_t callback,
                                   void *user_data);

// Error callback - invoked on parse errors
typedef void (*csv_error_callback_t)(csv_error_t error, const char *message,
                                      uint64_t row_number, void *user_data);
void csv_parser_set_error_callback(csv_parser_t *parser,
                                     csv_error_callback_t callback,
                                     void *user_data);
```

### Row and Field Access

```c
// Get field at index from row (returns NULL if out of bounds)
const csv_field_t *csv_get_field(const csv_row_t *row, size_t index);

// Get number of fields in row
size_t csv_get_num_fields(const csv_row_t *row);

// Field structure
typedef struct {
    const char *data;  // Pointer to field content (NOT null-terminated for unquoted!)
    size_t size;       // Length in bytes
    bool quoted;       // true if field was quoted in source
} csv_field_t;

// Row structure
typedef struct {
    csv_field_t *fields;   // Array of fields
    size_t num_fields;     // Number of fields
    uint64_t row_number;   // 1-based row number
    size_t byte_offset;    // Byte offset in input
} csv_row_t;
```

### Configuration Options

```c
csv_parse_options_t csv_default_options(void);

typedef struct {
    char delimiter;          // Field separator (default: ',')
    char quote_char;         // Quote character (default: '"')
    bool double_quote;       // Treat "" as escaped quote (default: true)
    bool trim_whitespace;    // Strip spaces from fields (default: false)
    bool ignore_empty_lines; // Skip blank lines (default: true)
    bool strict_mode;        // Error on malformed CSV (default: false)

    size_t max_field_size;   // Max bytes per field (default: 10MB)
    size_t max_row_size;     // Max bytes per row (default: 100MB)
    size_t buffer_size;      // I/O buffer size (default: 64KB)
    size_t max_memory_kb;    // Memory limit in KB, 0=unlimited (default: 0)

    bool disable_mmap;       // Force stream I/O instead of mmap (default: false)
    bool enable_prefetch;    // Use CPU prefetch hints (default: true)
} csv_parse_options_t;
```

### Error Handling

```c
typedef enum {
    CSV_OK = 0,
    CSV_ERROR_INVALID_ARGS = -1,
    CSV_ERROR_OUT_OF_MEMORY = -2,
    CSV_ERROR_PARSE_ERROR = -6,
    CSV_ERROR_FIELD_TOO_LARGE = -7,
    CSV_ERROR_ROW_TOO_LARGE = -8,
    CSV_ERROR_IO_ERROR = -9
} csv_error_t;

// Convert error code to human-readable string
const char *csv_error_string(csv_error_t error);
```

### Statistics and Diagnostics

```c
// Get performance statistics
csv_stats_t csv_parser_get_stats(const csv_parser_t *parser);

// Print formatted statistics to stdout
void csv_print_stats(const csv_parser_t *parser);

// Get detected SIMD features (bitmask of CSV_SIMD_* flags)
uint32_t csv_get_simd_features(void);
```

## Installation

### Header-Only

Copy `sonicsv.h` to your project:

```bash
curl -O https://raw.githubusercontent.com/vitruves/SonicSV/main/sonicsv.h
```

Or install system-wide:

```bash
git clone https://github.com/vitruves/SonicSV.git
cd SonicSV
make install          # installs to /usr/local/include
# or
make install PREFIX=/custom/path
```

### Building Examples and Tests

```bash
make test       # Build and run test suite
make example    # Build example program
make benchmark  # Compare against libcsv (requires libcsv)
make clean      # Remove build artifacts
```

## Advanced Usage

### Custom Options

```c
csv_parse_options_t opts = csv_default_options();
opts.delimiter = '\t';              // Tab-separated values
opts.trim_whitespace = true;        // Strip leading/trailing spaces
opts.strict_mode = true;            // Error on malformed CSV
opts.max_field_size = 1024 * 1024;  // 1MB field limit
opts.max_row_size = 10 * 1024 * 1024; // 10MB row limit
opts.buffer_size = 128 * 1024;      // 128KB I/O buffer

csv_parser_t *p = csv_parser_create(&opts);
```

### Streaming Large Files

```c
csv_parser_t *p = csv_parser_create(NULL);
csv_parser_set_row_callback(p, on_row, user_data);

FILE *fp = fopen("large_file.csv", "rb");
csv_parse_stream(p, fp);  // Processes file in chunks
fclose(fp);

csv_parser_destroy(p);
```

### Error Handling

```c
void on_error(csv_error_t err, const char *msg, uint64_t row, void *ctx) {
    fprintf(stderr, "Row %llu: %s (%s)\n", row, msg, csv_error_string(err));
}

csv_parser_set_error_callback(p, on_error, NULL);
```

### Performance Statistics

```c
csv_parse_file(p, "data.csv");

csv_stats_t stats = csv_parser_get_stats(p);
printf("Parsed %llu rows in %.3f ms\n",
       stats.total_rows_parsed,
       stats.parse_time_ns / 1e6);
printf("Throughput: %.2f MB/s\n", stats.throughput_mbps);

// Or use built-in formatting:
csv_print_stats(p);
```

### String Deduplication

For CSVs with repeated field values:

```c
csv_string_pool_t *pool = csv_string_pool_create(1024);

void on_row(const csv_row_t *row, void *ctx) {
    csv_string_pool_t *pool = (csv_string_pool_t *)ctx;

    for (size_t i = 0; i < row->num_fields; i++) {
        const csv_field_t *f = csv_get_field(row, i);
        const char *interned = csv_string_pool_intern(pool, f->data, f->size);
        // interned points to deduplicated string
    }
}

csv_parser_set_row_callback(p, on_row, pool);
// ...
csv_string_pool_destroy(pool);
```

## Supported Platforms

- **ARM64**: NEON (Apple Silicon, Raspberry Pi 4+, AWS Graviton)
- **x86_64**: SSE4.2, AVX2, AVX-512 (auto-detected at runtime)
- **Other architectures**: Optimized SWAR scalar implementation

Tested on Linux, macOS, and Windows (MSVC, MinGW).

## Thread Safety

Each `csv_parser_t` instance is completely isolated and thread-safe. You can:

- Use multiple parsers concurrently from different threads
- Parse different files in parallel
- Share read-only configuration structures

Do not share a single parser instance across threads without external synchronization.

## Contributing

Contributions are welcome. Please:

- Maintain C99 compatibility
- Add tests for new features
- Ensure `make test` passes
- Follow existing code style

## License

MIT License - see `sonicsv.h` for details.
