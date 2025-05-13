# SonicSV

![sonicsv2](https://github.com/user-attachments/assets/8d902700-57a9-443e-8097-3812442abb38)


SonicSV is a high-performance CSV parsing library written in C, designed for maximum efficiency when processing large datasets. The library provides a streaming API that allows processing CSV files without loading the entire content into memory.

## Version

Current version: 1.3.0

## Features

- Streaming API for memory-efficient processing
- Fast, optimized parsing with SIMD support (1.39-1.59x faster than libcsv)
- Support for standard CSV features like quotes and escaping
- Configurable delimiters and quote characters
- Multithreaded processing with configurable thread pool
- Memory-mapped file support for large files
- Minimal dependencies for maximum portability

## Getting Started

### Installation

```bash
# Build the library
make

# Install the library
sudo make install
```

### Basic Usage Example

```c
#include <sonicsv.h>
#include <stdio.h>

int main() {
    // 1. Create a default configuration
    SonicSVConfig config = sonicsv_default_config();
    
    // 2. Open a CSV file
    SonicSV* stream = sonicsv_open("data.csv", &config);
    if (!stream) {
        printf("Error: %s\n", sonicsv_get_error());
        return 1;
    }
    
    // 3. Iterate through rows
    SonicSVRow* row;
    while ((row = sonicsv_next_row(stream)) != NULL) {
        // 4. Access field count
        size_t fields = sonicsv_row_get_field_count(row);
        
        // 5. Access each field
        for (size_t i = 0; i < fields; i++) {
            const char* value = sonicsv_row_get_field(row, i);
            printf("%s%s", i > 0 ? "," : "", value);
        }
        printf("\n");
        
        // 6. Free the row when done
        sonicsv_row_free(row);
    }
    
    // 7. Close the stream
    sonicsv_close(stream);
    return 0;
}
```

### Multithreaded Processing Example

```c
#include <sonicsv.h>
#include <stdio.h>

// Define a row processing callback
void process_row(const SonicSVRow* row, void* user_data) {
    // Process each field
    size_t fields = sonicsv_row_get_field_count(row);
    for (size_t i = 0; i < fields; i++) {
        // Do something with sonicsv_row_get_field(row, i)
    }
    
    // Access user data if needed
    // int* counter = (int*)user_data;
    // (*counter)++;
}

int main() {
    // 1. Initialize the library (optional but recommended)
    sonicsv_init();
    
    // 2. Create configuration
    SonicSVConfig config = sonicsv_default_config();
    config.use_mmap = true;  // Use memory mapping for better performance
    
    // 3. Process the file in parallel
    int counter = 0;
    long rows_processed = sonicsv_process_parallel(
        "large_data.csv", 
        &config,
        process_row,
        &counter
    );
    
    if (rows_processed < 0) {
        printf("Error: %s\n", sonicsv_get_error());
    } else {
        printf("Processed %ld rows successfully\n", rows_processed);
    }
    
    // 4. Clean up
    sonicsv_cleanup();
    return 0;
}
```

## API Reference

### Configuration

```c
// Create default configuration with optimal settings
SonicSVConfig config = sonicsv_default_config();

// Configure parsing options
config.delimiter = ',';         // Field separator character
config.quote_char = '"';        // Quote character
config.trim_whitespace = false; // Whether to trim whitespace
config.skip_empty_lines = true; // Whether to skip empty lines

// Configure performance options
config.buffer_size = 64*1024;   // Read buffer size (0 = auto-detect)
config.use_mmap = true;         // Use memory mapping for large files
config.thread_safe = false;     // Thread-safety (costs performance)
```

### Core Operations

```c
// Open a CSV file
SonicSV* stream = sonicsv_open(filename, &config);

// Read the next row
SonicSVRow* row = sonicsv_next_row(stream);

// Get number of fields in current row
size_t field_count = sonicsv_row_get_field_count(row);

// Get field value as string
const char* value = sonicsv_row_get_field(row, field_index);

// Free row resources
sonicsv_row_free(row);

// Close the stream
sonicsv_close(stream);
```

### Multithreading Support

```c
// Initialize thread pool with optimal number of threads
bool success = sonicsv_thread_pool_init(sonicsv_get_optimal_threads());

// Process a file in parallel with a callback function
long rows = sonicsv_process_parallel(filename, &config, callback_function, user_data);

// Clean up thread pool when done
sonicsv_thread_pool_destroy();
```

### Utility Functions

```c
// Get the last error message
const char* error = sonicsv_get_error();

// Count rows in a file efficiently
long row_count = sonicsv_count_rows(filename, &config);

// Get library version
const char* version = sonicsv_get_version();
```

## Benchmarks

SonicSV is benchmarked against [libcsv](https://github.com/rgamble/libcsv), a mature and well-respected CSV parsing library in the C ecosystem. We are grateful to the libcsv team for their pioneering work, which has helped establish a solid foundation for CSV parsing in C.

### Benchmark Results

-- Comparative Performance Results (5 iterations) --
File: data/sample.csv
| Implementation | Min(s) | Avg(s) | Max(s) | Speedup vs LibCSV |
|----------------------|----------|----------|----------|------------------|
| LibCSV [ST] | 0.06 | 0.06 | 0.06 | 1.00x |
| SonicSV [ST] | 0.04 | 0.04 | 0.04 | 1.39x |
| LibCSV [MT] | 0.06 | 0.06 | 0.07 | 1.00x |
| SonicSV [MT] | 0.04 | 0.04 | 0.05 | 1.59x |


**Testing Environment:**
- Hardware: 2 × Intel(R) Xeon(R) E5-2680 0 (32) @ 2.70 GHz
- System: Ubuntu 24.04.2 LTS x86_64
- Host: Precision T7600 (01)
- Kernel: Linux 6.8.0-59-generic


## License

MIT License

## Acknowledgements

- The benchmark suite includes [libcsv](https://github.com/rgamble/libcsv) by Robert Gamble, which is licensed under LGPL-2.1
- Thanks to all contributors who have helped improve this library

## Repository

This project is hosted at [https://github.com/Vitruves/SonicSV](https://github.com/Vitruves/SonicSV)
