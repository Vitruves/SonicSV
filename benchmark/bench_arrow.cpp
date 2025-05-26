#include "arrow/cpp/src/arrow/csv/api.h"
#include "arrow/cpp/src/arrow/io/api.h"
#include "arrow/cpp/src/arrow/util/logging.h"
#include "arrow/cpp/src/arrow/compute/api.h"

#include <chrono>
#include <iostream>
#include <getopt.h>

// Print usage information
void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [options] <csv_file>\n";
    std::cerr << "Options:\n";
    std::cerr << "  -s, --stream       Use streaming parser (default)\n";
    std::cerr << "  -m, --mmap         Use memory-mapped parser (if available)\n";
    std::cerr << "  -p, --parallel     Enable multi-threaded parsing (default)\n";
    std::cerr << "  -S, --single       Use single-threaded parsing\n";
    std::cerr << "  -t, --threads=N    Use N threads (default: auto)\n";
    std::cerr << "  -h, --help         Show this help message\n";
}

int main(int argc, char** argv) {
    // Default options
    bool use_mmap = false;
    bool use_threads = true;
    int thread_count = 0; // Auto
    
    // Command line options
    static struct option long_options[] = {
        {"stream",   no_argument,       0, 's'},
        {"mmap",     no_argument,       0, 'm'},
        {"parallel", no_argument,       0, 'p'},
        {"single",   no_argument,       0, 'S'},
        {"threads",  required_argument, 0, 't'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "smpSt:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 's':
                use_mmap = false;
                break;
            case 'm':
                use_mmap = true;
                break;
            case 'p':
                use_threads = true;
                break;
            case 'S':
                use_threads = false;
                break;
            case 't':
                thread_count = atoi(optarg);
                if (thread_count < 0) {
                    std::cerr << "Error: Invalid thread count, using auto\n";
                    thread_count = 0;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Check for input file
    if (optind >= argc) {
        std::cerr << "Error: Missing input CSV file\n";
        print_usage(argv[0]);
    return 1;
  }

    const std::string filename = argv[optind];
    
    // Display parsing mode
    std::cout << "Parsing mode: " << (use_mmap ? "Memory-mapped" : "Streaming") << 
                ", Threading: " << (use_threads ? 
                  (thread_count > 0 ? "Multi-threaded (fixed)" : "Multi-threaded (auto)") 
                : "Single-threaded") << std::endl;

  auto start = std::chrono::steady_clock::now();

  // Open the file
    std::shared_ptr<arrow::io::ReadableFile> infile;
    
    if (use_mmap) {
        // Try memory mapping first
        try {
            // Use regular file I/O since we can't properly cast between memory mapped file and readable file
            auto file_result = arrow::io::ReadableFile::Open(filename);
            if (!file_result.ok()) {
                std::cerr << "Could not open file: " << file_result.status().ToString() << std::endl;
    return 1;
  }
            infile = file_result.ValueOrDie();
            std::cerr << "Note: Using standard file I/O instead of memory mapping due to C++ casting limitations" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error opening file: " << e.what() << std::endl;
            std::cerr << "Falling back to standard file I/O" << std::endl;
            
            auto file_result = arrow::io::ReadableFile::Open(filename);
            if (!file_result.ok()) {
                std::cerr << "Could not open file: " << file_result.status().ToString() << std::endl;
                return 1;
            }
            infile = file_result.ValueOrDie();
        }
    } else {
        // Use standard file I/O
        auto file_result = arrow::io::ReadableFile::Open(filename);
        if (!file_result.ok()) {
            std::cerr << "Could not open file: " << file_result.status().ToString() << std::endl;
            return 1;
        }
        infile = file_result.ValueOrDie();
    }

  auto read_options = arrow::csv::ReadOptions::Defaults();
    read_options.use_threads = use_threads;
    if (thread_count > 0) {
        read_options.block_size = 1024 * 1024;  // Adjust block size for threading
    }
    
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto convert_options = arrow::csv::ConvertOptions::Defaults();

  // Create IO context
  arrow::io::IOContext io_context(arrow::default_memory_pool());
  
  // Create table reader
  arrow::Result<std::shared_ptr<arrow::csv::TableReader>> reader_result = 
    arrow::csv::TableReader::Make(io_context, infile, read_options, parse_options, convert_options);
  if (!reader_result.ok()) {
    std::cerr << "Could not create table reader: " << reader_result.status().ToString() << std::endl;
    return 1;
  }
  std::shared_ptr<arrow::csv::TableReader> table_reader = reader_result.ValueOrDie();

  // Read the table
  arrow::Result<std::shared_ptr<arrow::Table>> table_result = table_reader->Read();
  if (!table_result.ok()) {
    std::cerr << "Could not read table: " << table_result.status().ToString() << std::endl;
    return 1;
  }
  std::shared_ptr<arrow::Table> table = table_result.ValueOrDie();

  double total_sum = 0.0;
  for (int i = 0; i < table->num_columns(); ++i) {
    const auto& chunked_array = table->column(i);
    if (chunked_array->type()->id() == arrow::Type::DOUBLE ||
        chunked_array->type()->id() == arrow::Type::FLOAT ||
        chunked_array->type()->id() == arrow::Type::INT64 ||
        chunked_array->type()->id() == arrow::Type::INT32) {
      for (const auto& chunk : chunked_array->chunks()) {
        // Try to sum numeric columns
        arrow::compute::ScalarAggregateOptions options;
        arrow::Result<arrow::Datum> sum_result = arrow::compute::Sum(chunk, options);
        if (sum_result.ok()) {
          auto datum = sum_result.ValueOrDie();
          if (datum.is_scalar()) {
            auto scalar = datum.scalar();
            if (scalar->is_valid) {
              // Cast to appropriate type based on data type
              if (scalar->type->id() == arrow::Type::DOUBLE) {
                auto double_scalar = std::static_pointer_cast<arrow::DoubleScalar>(scalar);
                total_sum += double_scalar->value;
              } else if (scalar->type->id() == arrow::Type::FLOAT) {
                auto float_scalar = std::static_pointer_cast<arrow::FloatScalar>(scalar);
                total_sum += float_scalar->value;
              } else if (scalar->type->id() == arrow::Type::INT64) {
                auto int64_scalar = std::static_pointer_cast<arrow::Int64Scalar>(scalar);
                total_sum += int64_scalar->value;
              } else if (scalar->type->id() == arrow::Type::INT32) {
                auto int32_scalar = std::static_pointer_cast<arrow::Int32Scalar>(scalar);
                total_sum += int32_scalar->value;
              }
            }
          }
        }
      }
    }
  }

  auto end = std::chrono::steady_clock::now();
  auto elapsed = end - start;
  double elapsed_seconds = std::chrono::duration<double>(elapsed).count();

    std::cout << "Parsed " << table->num_rows() << " rows in "
              << elapsed_seconds << " seconds ("
              << (table->num_rows() / elapsed_seconds) << " rows/sec)" << std::endl;
    std::cout << "Total sum: " << total_sum << std::endl;
  return 0;
}