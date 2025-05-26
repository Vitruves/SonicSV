# SonicSV - High-Performance Header-Only SIMD-Accelerated CSV Parser

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C Standard](https://img.shields.io/badge/C-C99+-blue.svg)](https://en.wikipedia.org/wiki/C99)
[![SIMD Support](https://img.shields.io/badge/SIMD-SSE4.2%20%7C%20AVX2%20%7C%20NEON-green.svg)](https://en.wikipedia.org/wiki/SIMD)

SonicSV is a portable, high-performance C library for parsing CSV and TSV files with optional SIMD acceleration. It provides both row-wise and batch processing capabilities, zero-copy field extraction, and is designed for large-scale data pipelines and parallel processing environments.

<p align="center">
  <img src="https://github.com/user-attachments/assets/fb08ff91-e282-44c6-a4e2-5384955a1d9c" width="369" alt="image-9">
</p>

## Key Features

### Performance

- **SIMD Acceleration**: Automatic detection and utilization of SSE4.2, AVX2 (x86_64), and NEON (ARM64) instructions
- **Zero-Copy Parsing**: Direct field access without unnecessary memory allocations
- **Batch Processing**: Efficient processing of large datasets with configurable batch sizes
- **Parallel Processing**: Multi-threaded row processing capabilities
- **Adaptive Parsing**: Automatic detection of optimal parsing mode (Simple, Quoted, TSV, Generic)

### Compatibility

- **Cross-Platform**: Linux, macOS, Windows support
- **Architecture Support**: x86_64 (Intel/AMD) and ARM64 (Apple Silicon, ARM servers)
- **C/C++ Compatible**: Pure C99 API with C++ compatibility
- **Header-Only Option**: Single-header implementation available

### Flexibility

- **Multiple Input Sources**: Files, streams, memory buffers
- **Configurable Options**: Customizable delimiters, quote characters, escape sequences
- **Error Handling**: Comprehensive error reporting with callbacks
- **Progress Tracking**: Real-time parsing progress and statistics
- **Memory Management**: Optional memory pooling for reduced allocations

### Benchmark Summary

The following results summarize the performance of various CSV parsers:

```
Overall Performance Summary:
--------------------------------------------------------------------------------
 1. sonicsv_batching (C)     Avg:  2066.56 MB/s Max:  4481.62 MB/s (100.0% scenarios)
 2. sonicsv_mt (C)          Avg:   580.93 MB/s Max:  1213.96 MB/s (100.0% scenarios)
 3. sonicsv_streaming (C)    Avg:   433.30 MB/s Max:   962.10 MB/s (100.0% scenarios)
 4. libcsv_mt (C)           Avg:   405.78 MB/s Max:   776.88 MB/s (100.0% scenarios)
 5. arrow (C++)               Avg:   303.16 MB/s Max:  1120.56 MB/s (100.0% scenarios)
 6. rapidcsv (C++)            Avg:   268.68 MB/s Max:   385.04 MB/s (100.0% scenarios)
 7. d99kris (C++)             Avg:   267.34 MB/s Max:   387.69 MB/s (100.0% scenarios)
 8. libcsv (C)              Avg:   112.07 MB/s Max:   143.24 MB/s (100.0% scenarios)
 9. semitrivial  (C)         Avg:    82.94 MB/s Max:   128.74 MB/s (100.0% scenarios)
10. ariasdiniz (C)          Avg:    25.64 MB/s Max:    71.74 MB/s (100.0% scenarios)
```
*Benchmarks performed on Intel Xeon E5-2680 @ 2.70GHz with 32 cores*

## Installation

### Using the Header-Only Version

```c
#define SONICSV_IMPLEMENTATION
#include "sonicsv.h"
```

### Building with CMake

```bash
git clone https://github.com/Vitruves/SonicSV.git
cd SonicSV
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Building with XMake

```bash
xmake build
xmake install
```

## 📖 Quick Start

### Basic CSV Parsing

```c
#include "sonicsv.h"

void row_callback(const csv_row_t *row, void *user_data) {
    printf("Row %lu: %u fields\n", row->row_number, row->num_fields);
    
    for (uint32_t i = 0; i < row->num_fields; i++) {
        printf("  Field %u: '%.*s'\n", i, 
               row->fields[i].size, row->fields[i].data);
    }
}

int main() {
    // Initialize SIMD features
    csv_simd_init();
    
    // Create parser with default options
    csv_parse_options_t options = csv_default_options();
    csv_parser_t *parser = csv_parser_create(&options);
    
    // Set callback
    csv_parser_set_row_callback(parser, row_callback, NULL);
    
    // Parse file
    csv_error_t result = csv_parse_file(parser, "data.csv");
    
    if (result == CSV_OK) {
        csv_print_stats(parser);
    } else {
        printf("Error: %s\n", csv_error_string(result));
    }
    
    csv_parser_destroy(parser);
    return 0;
}
```

### High-Performance Batch Processing

```c
#include "sonicsv.h"

void batch_callback(const csv_data_batch_t *batch, void *user_data) {
    printf("Processed batch: %u rows, %u total fields\n", 
           batch->num_rows, batch->num_fields_total);
    
    // Process batch data efficiently
    for (uint32_t row = 0; row < batch->num_rows; row++) {
        // Access fields using zero-copy batch API
        const char *field_data;
        uint32_t field_size;
        bool quoted;
        
        csv_batch_get_field(batch, row, 0, &field_data, &field_size, &quoted);
        // Process field data...
    }
}

int main() {
    csv_simd_init();
    
    // Configure for high-performance batch processing
    csv_block_config_t config = csv_default_block_config();
    config.block_size = 1024 * 1024;  // 1MB blocks
    config.max_rows_per_batch = 50000;
    config.parallel_processing = true;
    config.num_threads = 8;
    
    csv_block_parser_t *parser = csv_block_parser_create(&config);
    csv_block_parser_set_batch_callback(parser, batch_callback, NULL);
    
    csv_error_t result = csv_block_parse_file(parser, "large_dataset.csv");
    
    if (result == CSV_OK) {
        csv_advanced_stats_t stats = csv_block_parser_get_stats(parser);
        printf("Throughput: %.2f MB/s\n", 
               stats.avg_block_parse_time_ms > 0 ? 
               (config.block_size / 1024.0 / 1024.0) / 
               (stats.avg_block_parse_time_ms / 1000.0) : 0.0);
    }
    
    csv_block_parser_destroy(parser);
    return 0;
}
```

### TSV Processing with SIMD Optimization

```c
#include "sonicsv.h"

int main() {
    csv_simd_init();
    
    // TSV files are automatically detected and optimized
    csv_parse_options_t options = csv_default_options();
    options.delimiter = '\t';  // Explicitly set for TSV
    
    csv_parser_t *parser = csv_parser_create(&options);
    
    // The parser will automatically use TSV-optimized SIMD routines
    csv_error_t result = csv_parse_file(parser, "data.tsv");
    
    if (result == CSV_OK) {
        csv_stats_t stats = csv_parser_get_stats(parser);
        printf("SIMD features used: ");
        if (stats.simd_acceleration_used & CSV_SIMD_AVX2) printf("AVX2 ");
        if (stats.simd_acceleration_used & CSV_SIMD_SSE4_2) printf("SSE4.2 ");
        if (stats.simd_acceleration_used & CSV_SIMD_NEON) printf("NEON ");
        printf("\nThroughput: %.2f MB/s\n", stats.throughput_mbps);
    }
    
    csv_parser_destroy(parser);
    return 0;
}
```

### Multithreading and Streaming

SonicSV provides two distinct approaches for high-performance CSV processing:

#### 1. Built-in Multithreading (Block Parser)

The `csv_block_parser_t` provides native multithreading support with automatic workload distribution:

```c
#include "sonicsv.h"

void batch_callback(const csv_data_batch_t *batch, void *user_data) {
    printf("Processing batch with %u rows across %u threads\n", 
           batch->num_rows, *(uint32_t*)user_data);
    
    // Process batch data in parallel-safe manner
    for (uint32_t row = 0; row < batch->num_rows; row++) {
        // Zero-copy field access
        const char *field_data;
        uint32_t field_size;
        bool quoted;
        csv_batch_get_field(batch, row, 0, &field_data, &field_size, &quoted);
        // Process field...
    }
}

int main() {
    csv_simd_init();
    
    // Configure multithreaded block processing
    csv_block_config_t config = csv_default_block_config();
    config.parallel_processing = true;
    config.num_threads = 8;                    // Use 8 worker threads
    config.block_size = 2 * 1024 * 1024;      // 2MB blocks for optimal throughput
    config.max_rows_per_batch = 100000;       // Large batches for efficiency
    config.use_memory_pool = true;            // Reduce allocation overhead
    
    csv_block_parser_t *parser = csv_block_parser_create(&config);
    uint32_t thread_count = config.num_threads;
    csv_block_parser_set_batch_callback(parser, batch_callback, &thread_count);
    
    csv_error_t result = csv_block_parse_file(parser, "large_dataset.csv");
    
    if (result == CSV_OK) {
        csv_advanced_stats_t stats = csv_block_parser_get_stats(parser);
        printf("Processed %lu blocks using %u threads\n", 
               stats.total_blocks_processed, config.num_threads);
        printf("Average throughput: %.2f MB/s\n", 
               stats.avg_block_parse_time_ms > 0 ? 
               (config.block_size / 1024.0 / 1024.0) / 
               (stats.avg_block_parse_time_ms / 1000.0) : 0.0);
    }
    
    csv_block_parser_destroy(parser);
    return 0;
}
```

#### 2. Streaming Parser (Single-threaded, Memory Efficient)

The `csv_parser_t` provides streaming capabilities for memory-constrained environments:

```c
#include "sonicsv.h"

typedef struct {
    uint64_t processed_rows;
    uint64_t processed_bytes;
    FILE *output_file;
} stream_context_t;

void row_callback(const csv_row_t *row, void *user_data) {
    stream_context_t *ctx = (stream_context_t*)user_data;
    
    // Process row immediately (streaming)
    fprintf(ctx->output_file, "Row %lu: ", row->row_number);
    for (uint32_t i = 0; i < row->num_fields; i++) {
        fprintf(ctx->output_file, "'%.*s'", 
                row->fields[i].size, row->fields[i].data);
        if (i < row->num_fields - 1) fprintf(ctx->output_file, ", ");
    }
    fprintf(ctx->output_file, "\n");
    
    ctx->processed_rows++;
}

void progress_callback(uint64_t bytes_processed, uint64_t total_bytes, void *user_data) {
    stream_context_t *ctx = (stream_context_t*)user_data;
    ctx->processed_bytes = bytes_processed;
    
    if (total_bytes > 0) {
        double progress = (double)bytes_processed / total_bytes * 100.0;
        printf("\rProgress: %.1f%% (%lu rows processed)", progress, ctx->processed_rows);
        fflush(stdout);
    }
}

int main() {
    csv_simd_init();
    
    // Configure streaming parser for memory efficiency
    csv_parse_options_t options = csv_default_options();
    options.max_field_size = 64 * 1024;       // Limit field size for streaming
    options.max_row_size = 1024 * 1024;       // Limit row size
    
    csv_parser_t *parser = csv_parser_create(&options);
    
    stream_context_t ctx = {0, 0, stdout};
    csv_parser_set_row_callback(parser, row_callback, &ctx);
    csv_parser_set_progress_callback(parser, progress_callback, &ctx);
    
    // Stream processing - minimal memory footprint
    csv_error_t result = csv_parse_file(parser, "large_dataset.csv");
    
    if (result == CSV_OK) {
        csv_stats_t stats = csv_parser_get_stats(parser);
        printf("\nStreaming completed: %lu rows, %.2f MB/s\n", 
               stats.total_rows_parsed, stats.throughput_mbps);
    }
    
    csv_parser_destroy(parser);
    return 0;
}
```

#### 3. Manual Multithreading with File Chunking

For custom threading scenarios, you can implement manual file splitting:

```c
#include "sonicsv.h"
#include <pthread.h>

typedef struct {
    const char *data;
    size_t size;
    uint32_t thread_id;
    uint64_t *row_count;
    pthread_mutex_t *mutex;
} thread_data_t;

void *worker_thread(void *arg) {
    thread_data_t *data = (thread_data_t*)arg;
    
    csv_parser_t *parser = csv_parser_create(NULL);
    uint64_t local_rows = 0;
    
    // Set up local row counter
    csv_parser_set_row_callback(parser, 
        [](const csv_row_t *row, void *user_data) {
            (*(uint64_t*)user_data)++;
        }, &local_rows);
    
    // Process chunk
    csv_parse_buffer(parser, data->data, data->size, true);
    
    // Update global counter thread-safely
    pthread_mutex_lock(data->mutex);
    *(data->row_count) += local_rows;
    pthread_mutex_unlock(data->mutex);
    
    printf("Thread %u processed %lu rows\n", data->thread_id, local_rows);
    
    csv_parser_destroy(parser);
    return NULL;
}

int main() {
    csv_simd_init();
    
    // Load file into memory for chunking
    FILE *file = fopen("large_dataset.csv", "rb");
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *file_data = malloc(file_size);
    fread(file_data, 1, file_size, file);
    fclose(file);
    
    // Split into chunks with line boundary detection
    const uint32_t num_threads = 4;
    size_t chunk_size = file_size / num_threads;
    
    pthread_t threads[num_threads];
    thread_data_t thread_data[num_threads];
    uint64_t total_rows = 0;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    
    for (uint32_t i = 0; i < num_threads; i++) {
        size_t start = i * chunk_size;
        size_t end = (i == num_threads - 1) ? file_size : (i + 1) * chunk_size;
        
        // Adjust chunk boundaries to line boundaries
        if (i > 0) {
            while (start < file_size && file_data[start] != '\n') start++;
            if (start < file_size) start++; // Skip the newline
        }
        if (i < num_threads - 1) {
            while (end < file_size && file_data[end] != '\n') end++;
        }
        
        thread_data[i] = (thread_data_t){
            .data = file_data + start,
            .size = end - start,
            .thread_id = i,
            .row_count = &total_rows,
            .mutex = &mutex
        };
        
        pthread_create(&threads[i], NULL, worker_thread, &thread_data[i]);
    }
    
    // Wait for all threads to complete
    for (uint32_t i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("Total rows processed: %lu\n", total_rows);
    
    free(file_data);
    pthread_mutex_destroy(&mutex);
    return 0;
}
```

#### When to Use Each Approach

**Use Built-in Multithreading (`csv_block_parser_t`) when:**
- Processing large files (>100MB)
- You have multiple CPU cores available
- You need maximum throughput
- Memory usage is not a primary concern

**Use Streaming (`csv_parser_t`) when:**
- Memory is limited
- Processing files larger than available RAM
- You need real-time processing with immediate results
- Single-threaded environment

**Use Manual Multithreading when:**
- You need custom threading logic
- Integrating with existing thread pools
- You want fine-grained control over work distribution
- Processing multiple files simultaneously

## 🔧 Configuration Options

### Parse Options

```c
typedef struct {
    char delimiter;           // Field delimiter (default: ',')
    char quote_char;          // Quote character (default: '"')
    char escape_char;         // Escape character (default: '\\')
    bool quoting;             // Enable quote processing (default: true)
    bool escaping;            // Enable escape processing (default: false)
    bool double_quote;        // Handle double quotes (default: true)
    bool newlines_in_values;  // Allow newlines in quoted fields (default: false)
    bool ignore_empty_lines;  // Skip empty lines (default: true)
    bool trim_whitespace;     // Trim field whitespace (default: false)
    int32_t max_field_size;   // Maximum field size (default: 1MB)
    int32_t max_row_size;     // Maximum row size (default: 10MB)
} csv_parse_options_t;
```

### Block Processing Configuration

```c
typedef struct {
    csv_parse_options_t parse_options;  // Base parsing options
    uint32_t block_size;                // Processing block size (default: 64KB)
    uint32_t max_rows_per_batch;        // Maximum rows per batch (default: 10000)
    bool parallel_processing;           // Enable parallel processing (default: false)
    uint32_t num_threads;               // Number of processing threads (default: 1)
    bool use_memory_pool;               // Use memory pooling (default: false)
    uint32_t initial_pool_size;         // Initial pool size (default: 1MB)
} csv_block_config_t;
```

## 📈 Performance Tuning

### SIMD Optimization

```c
// Check available SIMD features
uint32_t features = csv_detect_simd_features();
if (features & CSV_SIMD_AVX2) {
    printf("AVX2 acceleration available\n");
}

// Initialize SIMD subsystem
csv_simd_init();
```

### Memory Management

```c
// Use string pooling for repeated field values
csv_string_pool_t *pool = csv_string_pool_create(1024 * 1024);

// Intern frequently used strings
const char *interned = csv_string_pool_intern(pool, "common_value", 12);

csv_string_pool_destroy(pool);
```

### Parallel Processing

```c
// Configure for maximum throughput
csv_block_config_t config = csv_default_block_config();
config.parallel_processing = true;
config.num_threads = 16;  // Match your CPU core count
config.block_size = 2 * 1024 * 1024;  // 2MB blocks for large files
```

## 🔍 Error Handling

SonicSV provides comprehensive error handling:

```c
void error_callback(csv_error_t error, const char *message, 
                   uint64_t row_number, void *user_data) {
    fprintf(stderr, "Parse error at row %lu: %s (%s)\n", 
            row_number, message, csv_error_string(error));
}

csv_parser_set_error_callback(parser, error_callback, NULL);
```

### Error Codes

- `CSV_OK`: Success
- `CSV_ERROR_INVALID_ARGS`: Invalid function arguments
- `CSV_ERROR_OUT_OF_MEMORY`: Memory allocation failure
- `CSV_ERROR_BUFFER_TOO_SMALL`: Buffer size insufficient
- `CSV_ERROR_INVALID_FORMAT`: Invalid CSV format
- `CSV_ERROR_IO_ERROR`: File I/O error
- `CSV_ERROR_PARSE_ERROR`: General parsing error

## 🧪 Testing and Benchmarks

### Running Tests

```bash
# Build and run tests
make test
```

### Running Benchmarks

SonicSV includes comprehensive benchmarks comparing against multiple CSV parsers:

```bash
# Download benchmark dependencies
chmod +x scripts/download-deps.sh
./scripts/download-deps.sh

# Run quick benchmark suite (Python-based)
python3 benchmark_suite.py [options]

# Run comprehensive benchmarks (Go-based, recommended)
go run benchmark_runner.go [options]

# Run benchmarks without rebuilding (if already compiled)
go run benchmark_runner.go --skip-build

# Quick benchmark run with fewer iterations
go run benchmark_runner.go --quick --iterations 1
```

### Benchmark Features

- **Multiple Parsers**: Compares SonicSV against Arrow, libcsv, rapidcsv, and others
- **Threading Variants**: Tests single-threaded, multithreaded, and streaming modes
- **Performance Metrics**: Measures throughput, memory usage, and parsing accuracy
- **Automated Analysis**: Generates detailed performance reports and comparisons
- **SIMD Detection**: Automatically detects and utilizes available SIMD features


## 📄 License

SonicSV is released under the MIT License. See [LICENSE](LICENSE) for details.

## 🙏 Acknowledgments

- Inspired by high-performance CSV parsers like simdjson
- SIMD optimization techniques from various open-source projects
- Community feedback and contributions

## 📞 Support

- **Issues**: [GitHub Issues](https://github.com/Vitruves/SonicSV/issues)

---

**SonicSV** - *Parsing CSV at the speed of sound* 🎵
