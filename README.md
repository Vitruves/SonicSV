# SonicSV

SonicSV is a high-performance CSV parsing library written in C, designed for maximum efficiency when processing large datasets. The library provides a streaming API that allows processing CSV files without loading the entire content into memory.

## Benchmarks

SonicSV outperforms the standard libcsv library by approximately 1.6x in parsing speed:

| Library | Speed (rows/second) | Relative Speed |
|---------|---------------------|----------------|
| SonicSV | ~1.58 million       | 1.00x          |
| libcsv  | ~0.97 million       | 0.62x          |

*Benchmark system: DELL Precision T7600 with Intel(R) Xeon(R) E5-2680 0 (32) @ 2.70GHz*

## Features

- Streaming API for memory-efficient processing
- Fast, optimized parsing
- Support for standard CSV features like quotes and escaping
- Configurable delimiters and quote characters
- Minimal dependencies for maximum portability

## Usage

```c
#include <sonicsv.h>
#include <stdio.h>

int main() {
    // Get default configuration
    SonicSVConfig config = sonicsv_default_config();
    
    // Open CSV file
    SonicSV* stream = sonicsv_open("data.csv", &config);
    if (!stream) {
        printf("Error: %s\n", sonicsv_get_error());
        return 1;
    }
    
    // Process each row
    SonicSVRow* row;
    while ((row = sonicsv_next_row(stream)) != NULL) {
        // Get number of fields
        size_t fields = sonicsv_row_get_field_count(row);
        
        // Process each field
        for (size_t i = 0; i < fields; i++) {
            printf("%s%s", i > 0 ? "," : "", sonicsv_row_get_field(row, i));
        }
        printf("\n");
        
        // Free row memory
        sonicsv_row_free(row);
    }
    
    // Close stream
    sonicsv_close(stream);
    return 0;
}
```

## Building

```bash
# Build the library
make

# Run benchmarks
make benchmark

# Install the library
sudo make install
```

## Comparison with other libraries

For benchmarking, we compare SonicSV against [libcsv](https://github.com/rgamble/libcsv), a popular and robust CSV parsing library written in ANSI C.

SonicSV is designed from the ground up for maximum performance while maintaining a clean API and reliability. The benchmark results show that SonicSV processes CSV data approximately 1.6 times faster than libcsv when processing large datasets.

## License

MIT License

## Acknowledgements

- The benchmark suite includes [libcsv](https://github.com/rgamble/libcsv) by Robert Gamble, which is licensed under LGPL-2.1
- Thanks to all contributors who have helped improve this library

## Repository

This project is hosted at [https://github.com/Vitruves/SonicSV](https://github.com/Vitruves/SonicSV)