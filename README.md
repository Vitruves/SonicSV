# SonicSV

A fast, single-header CSV parser for C with SIMD acceleration.

<img width="369" height="262" alt="image-9" src="https://github.com/user-attachments/assets/a4c60361-5745-4c2b-a21f-ca363dbd2634" />

## Why SonicSV?

SonicSV is a drop-in CSV parser that's typically **2-6x faster** than libcsv for common workloads. It uses SIMD instructions (NEON on ARM, SSE/AVX on x86) to accelerate parsing.

**Best suited for:**
- Processing large CSV files (logs, data exports, analytics)
- Applications where CSV parsing is a bottleneck
- Projects that want a simple, header-only dependency

**When to stick with libcsv:**
- Complex quoted fields with many embedded commas (SonicSV is only ~1.5x faster here)
- If you need a battle-tested library with decades of production use
- If you're already using libcsv and performance is acceptable

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

| Test Case | SonicSV | libcsv | Speedup |
|-----------|---------|--------|---------|
| Simple CSV (no quotes) | 1.9 GB/s | 320 MB/s | **6x** |
| Long fields | 3.7 GB/s | 355 MB/s | **10x** |
| Very long fields | 5.5 GB/s | 364 MB/s | **15x** |
| Quoted with commas | 494 MB/s | 331 MB/s | **1.5x** |
| Quoted with newlines | 980 MB/s | 360 MB/s | **2.7x** |

**Aggregate: 5.6x faster** (230 MB in 0.12s vs 0.69s)

The speedup is highest for simple, unquoted CSV data. Complex quoted fields with embedded delimiters see smaller gains.

## API

### Parsing

```c
csv_parser_t *csv_parser_create(NULL);           // Create parser
csv_parser_set_row_callback(p, callback, ctx);   // Set row handler
csv_parse_file(p, "file.csv");                   // Parse file
csv_parse_string(p, "a,b,c\n1,2,3\n");           // Parse string
csv_parser_destroy(p);                           // Cleanup
```

### Row Callback

```c
void on_row(const csv_row_t *row, void *user_data) {
    for (size_t i = 0; i < row->num_fields; i++) {
        const csv_field_t *field = csv_get_field(row, i);
        // field->data = pointer to field content
        // field->size = length in bytes
        // field->quoted = true if field was quoted
    }
}
```

### Options

```c
csv_parse_options_t opts = csv_default_options();
opts.delimiter = ';';           // European CSV
opts.delimiter = '\t';          // TSV
opts.trim_whitespace = true;    // Strip spaces from fields
opts.strict_mode = true;        // Error on malformed CSV

csv_parser_t *p = csv_parser_create(&opts);
```

### Error Handling

```c
csv_error_t err = csv_parse_file(p, "file.csv");
if (err != CSV_OK) {
    printf("Error: %s\n", csv_error_string(err));
}
```

## Building

```bash
make test       # Run test suite
make benchmark  # Compare against libcsv (requires: brew install libcsv)
make example    # Run example program
```

## Supported Platforms

- **ARM64**: NEON (Apple Silicon, Raspberry Pi 4+, AWS Graviton)
- **x86_64**: SSE4.2, AVX2, AVX-512 (auto-detected at runtime)
- **Other**: Falls back to optimized scalar code

## License

MIT License - see `sonicsv.h` for details.
