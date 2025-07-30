# SonicSV - High-Performance Header-Only SIMD-Accelerated CSV Parser

A fast, single-header CSV parser library written in C with SIMD optimizations and comprehensive error handling.

<img width="369" height="262" alt="image-9" src="https://github.com/user-attachments/assets/a4c60361-5745-4c2b-a21f-ca363dbd2634" />


## Performance

SonicSV consistently outperforms libcsv across all test configurations on both ARM and x86_64 architectures (average of 10 runs):

### MacBook Air M3 (ARM64/NEON on 13-inch, M3, 2024)

| Configuration  | SonicSV (MB/s) | libcsv (MB/s) | Speedup         |
| -------------- | -------------- | ------------- | --------------- |
| Small Simple   | 461.2          | 166.6         | **2.77x** |
| Small Medium   | 609.0          | 293.3         | **2.08x** |
| Small Complex  | 967.7          | 378.9         | **2.55x** |
| Medium Simple  | 726.6          | 303.4         | **2.40x** |
| Medium Medium  | 772.8          | 313.1         | **2.47x** |
| Medium Complex | 1004.5         | 362.6         | **2.77x** |
| Large Simple   | 708.1          | 289.9         | **2.44x** |
| Large Medium   | 752.7          | 300.1         | **2.51x** |
| Large Complex  | 923.7          | 359.7         | **2.57x** |
| XLarge Simple  | 702.9          | 285.0         | **2.47x** |
| XLarge Medium  | 750.4          | 298.4         | **2.51x** |
| Giant Simple   | 702.3          | 285.1         | **2.46x** |

**ARM64 Summary**: Average speedup of **2.5x** across 12 configurations

### Linux x86_64 (SSE4.2/AVX on Intel Xeon E5-2680 @ 2.70 GHz)

| Configuration  | SonicSV (MB/s) | libcsv (MB/s) | Speedup         |
| -------------- | -------------- | ------------- | --------------- |
| Small Simple   | 256.9          | 86.3          | **2.98x** |
| Small Medium   | 206.2          | 99.2          | **2.08x** |
| Small Complex  | 162.1          | 150.0         | **1.08x** |
| Medium Simple  | 247.6          | 97.1          | **2.55x** |
| Medium Medium  | 208.4          | 111.8         | **1.86x** |
| Medium Complex | 188.3          | 154.4         | **1.22x** |
| Large Simple   | 317.2          | 124.9         | **2.54x** |
| Large Medium   | 279.9          | 130.4         | **2.15x** |
| Large Complex  | 176.7          | 155.0         | **1.14x** |
| XLarge Simple  | 330.6          | 126.2         | **2.62x** |
| XLarge Medium  | 279.2          | 132.0         | **2.11x** |
| Giant Simple   | 326.1          | 125.9         | **2.59x** |

**x86_64 Summary**: Average speedup of **2.0x** across 12 configurations


## Features

- **SIMD Acceleration**: Automatic detection and use of SSE4.2, AVX2, AVX-512, NEON, and SVE
- **Memory Efficient**: Optimized memory allocation with recycling and alignment
- **Thread Safe**: Per-parser state isolation with atomic operations
- **Streaming Support**: Process large files without loading entirely into memory
- **Flexible Options**: Configurable delimiters, quote handling, and parsing modes
- **Comprehensive Error Handling**: Detailed error reporting with line numbers
- **Single Header**: Easy integration with `#define SONICSV_IMPLEMENTATION`

## Quick Start

```c
#define SONICSV_IMPLEMENTATION
#include <sonicsv.h>

void row_callback(const csv_row_t *row, void *user_data) {
    for (size_t i = 0; i < row->num_fields; i++) {
        const csv_field_t *field = csv_get_field(row, i);
        printf("Field %zu: '%.*s'\n", i, (int)field->size, field->data);
    }
}

int main() {
    csv_parser_t *parser = csv_parser_create(NULL);
    if (!parser) return 1;
  
    csv_parser_set_row_callback(parser, row_callback, NULL);
  
    const char *csv_data = "name,age,city\nJohn,25,New York\nJane,30,Boston\n";
    csv_error_t result = csv_parse_string(parser, csv_data);
  
    if (result != CSV_OK) {
        fprintf(stderr, "Parse error: %s\n", csv_error_string(result));
        csv_parser_destroy(parser);
        return 1;
    }
  
    csv_print_stats(parser);
    csv_parser_destroy(parser);
    return 0;
}
```

## API Reference

### Core Functions

#### Parser Management

```c
csv_parser_t *csv_parser_create(const csv_parse_options_t *options);
void csv_parser_destroy(csv_parser_t *parser);
csv_error_t csv_parser_reset(csv_parser_t *parser);
```

#### Parsing Functions

```c
csv_error_t csv_parse_file(csv_parser_t *parser, const char *filename);
csv_error_t csv_parse_stream(csv_parser_t *parser, FILE *stream);
csv_error_t csv_parse_string(csv_parser_t *parser, const char *csv_line);
csv_error_t csv_parse_buffer(csv_parser_t *parser, const char *buffer, size_t size, bool is_final);
```

#### Callbacks

```c
void csv_parser_set_row_callback(csv_parser_t *parser, csv_row_callback_t callback, void *user_data);
void csv_parser_set_error_callback(csv_parser_t *parser, csv_error_callback_t callback, void *user_data);
```

### Configuration Options

```c
csv_parse_options_t opts = csv_default_options();
opts.delimiter = ';';              // Use semicolon as delimiter
opts.quote_char = '\'';             // Use single quotes
opts.trim_whitespace = true;        // Trim whitespace from fields
opts.strict_mode = true;            // Enable strict parsing
opts.max_field_size = 1024 * 1024;  // 1MB max field size
opts.max_row_size = 10 * 1024 * 1024; // 10MB max row size
opts.buffer_size = 64 * 1024;       // 64KB buffer size
opts.enable_parallel = true;        // Enable parallel processing
opts.enable_prefetch = true;        // Enable memory prefetching

csv_parser_t *parser = csv_parser_create(&opts);
```

### File Parsing

```c
csv_error_t result = csv_parse_file(parser, "data.csv");
if (result != CSV_OK) {
    fprintf(stderr, "File parse error: %s\n", csv_error_string(result));
}
```

### Stream Parsing

```c
FILE *fp = fopen("large_file.csv", "rb");
if (fp) {
    csv_error_t result = csv_parse_stream(parser, fp);
    fclose(fp);
}
```

### Error Handling

```c
void error_callback(csv_error_t error, const char *message, 
                    uint64_t row_number, void *user_data) {
    fprintf(stderr, "Error on row %llu: %s (%s)\n", 
            row_number, message, csv_error_string(error));
}

csv_parser_set_error_callback(parser, error_callback, NULL);
```

### Field Access

```c
void row_callback(const csv_row_t *row, void *user_data) {
    size_t num_fields = csv_get_num_fields(row);
  
    for (size_t i = 0; i < num_fields; i++) {
        const csv_field_t *field = csv_get_field(row, i);
        if (field) {
            printf("Field %zu: '%.*s' (quoted: %s)\n", 
                   i, (int)field->size, field->data, 
                   field->quoted ? "yes" : "no");
        }
    }
}
```

### Performance Statistics

```c
csv_stats_t stats = csv_parser_get_stats(parser);
printf("Throughput: %.2f MB/s\n", stats.throughput_mbps);
printf("Rows parsed: %llu\n", (unsigned long long)stats.total_rows_parsed);
printf("Peak memory: %u KB\n", stats.peak_memory_kb);

// Or use built-in stats printer
csv_print_stats(parser);
```

### SIMD Feature Detection

```c
uint32_t features = csv_get_simd_features();
if (features & CSV_SIMD_AVX2) {
    printf("AVX2 acceleration available\n");
}
if (features & CSV_SIMD_NEON) {
    printf("NEON acceleration available\n");
}
```

## Error Codes

| Code                          | Description            |
| ----------------------------- | ---------------------- |
| `CSV_OK`                    | Success                |
| `CSV_ERROR_INVALID_ARGS`    | Invalid arguments      |
| `CSV_ERROR_OUT_OF_MEMORY`   | Out of memory          |
| `CSV_ERROR_PARSE_ERROR`     | Parse error            |
| `CSV_ERROR_FIELD_TOO_LARGE` | Field exceeds max size |
| `CSV_ERROR_ROW_TOO_LARGE`   | Row exceeds max size   |
| `CSV_ERROR_IO_ERROR`        | I/O error              |

## Compilation

```bash
gcc -O3 -march=native -DSONICSV_IMPLEMENTATION your_program.c -o your_program
```

For maximum performance on x86_64:

```bash
gcc -O3 -march=native -mavx2 -msse4.2 -DSONICSV_IMPLEMENTATION your_program.c
```

For ARM (Apple Silicon/Raspberry Pi):

```bash
gcc -O3 -mcpu=native -DSONICSV_IMPLEMENTATION your_program.c
```

## License

MIT License - see header file for full license text.

## Thread Safety

Each parser instance is isolated and thread-safe. Multiple parsers can be used concurrently without interference. SIMD feature detection uses atomic operations for thread safety.

## Memory Management

SonicSV uses optimized memory allocation with:

- Size-class based recycling pools
- Cache-aligned allocations for SIMD operations
- Automatic memory tracking and peak usage reporting
- Configurable memory limits

## Architecture Support

- **x86_64**: SSE4.2, AVX2, AVX-512 with runtime detection
- **ARM64**: NEON standard, SVE when available
- **Other**: Automatic fallback to optimized scalar code

SonicSV automatically detects and uses the best available SIMD instruction set for optimal performance on your hardware.
