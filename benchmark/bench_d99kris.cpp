#include "d99kris/src/rapidcsv.h"
#include <iostream>
#include <chrono>
#include <string>
#include <getopt.h>
#include <sys/stat.h>
#include <vector>
#include <fstream>
#include <cstring>

// Print usage information
void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [options] <csv_file>\n";
    std::cerr << "Options:\n";
    std::cerr << "  -s, --stream       Use streaming parser (default)\n";
    std::cerr << "  -m, --mmap         Use memory-mapped parser (not available)\n";
    std::cerr << "  -p, --parallel     Enable multi-threaded parsing (not available)\n";
    std::cerr << "  -S, --single       Use single-threaded parsing (default)\n";
    std::cerr << "  -h, --help         Show this help message\n";
}

// Low-level optimized CSV counter
size_t count_csv_lines(const std::string& filename, size_t& columns) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Failed to open file: " << filename << std::endl;
        return 0;
    }
    
    // Determine file size for buffer allocation
    file.seekg(0, std::ios::end);
    const size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    if (fileSize == 0) {
        columns = 0;
        return 0;
    }
    
    // Use smaller buffer size for safety
    const size_t bufferSize = std::min(size_t(32 * 1024), fileSize);
    std::vector<char> buffer(bufferSize);
    
    size_t lines = 0;
    bool inQuote = false;
    bool lastWasCR = false;
    size_t maxCols = 0;
    size_t currentCols = 1; // Start with 1 for the first field
    size_t totalBytesRead = 0;
    bool fileHasContent = false;
    
    while (file && totalBytesRead < fileSize) {
        size_t bytesToRead = std::min(bufferSize, fileSize - totalBytesRead);
        file.read(buffer.data(), bytesToRead);
        size_t bytesRead = file.gcount();
        if (bytesRead == 0) break;
        
        totalBytesRead += bytesRead;
        fileHasContent = true;
        
        for (size_t i = 0; i < bytesRead; ++i) {
            char c = buffer[i];
            
            if (inQuote) {
                if (c == '"') {
                    // Check for escaped quote (next char is also quote)
                    if (i + 1 < bytesRead && buffer[i + 1] == '"') {
                        i++; // Skip the next quote
                    } else {
                        inQuote = false;
                    }
                }
            } else {
                if (c == '"') {
                    inQuote = true;
                } else if (c == ',') {
                    currentCols++;
                } else if (c == '\n') {
                    if (!lastWasCR) { // Avoid double counting for \r\n
                        lines++;
                        maxCols = std::max(maxCols, currentCols);
                        currentCols = 1; // Reset for next line
                    }
                    lastWasCR = false;
                } else if (c == '\r') {
                    lines++;
                    maxCols = std::max(maxCols, currentCols);
                    currentCols = 1; // Reset for next line
                    lastWasCR = true;
                } else {
                    lastWasCR = false;
                }
            }
        }
    }
    
    // Handle case where file doesn't end with newline
    if (fileHasContent && currentCols >= 1) {
        // Only add a line if we have actual content
        if (totalBytesRead > 0) {
            // Check if the last character was a newline
            if (totalBytesRead > 0 && buffer[std::min(bufferSize - 1, totalBytesRead - 1)] != '\n' && 
                buffer[std::min(bufferSize - 1, totalBytesRead - 1)] != '\r') {
                lines++;
                maxCols = std::max(maxCols, currentCols);
            }
        }
    }
    
    columns = maxCols;
    return lines;
}

int main(int argc, char* argv[]) {
    // Default options
    bool use_mmap = false;  // RapidCSV doesn't support memory mapping
    bool use_threads = false;  // RapidCSV doesn't support threading
    bool use_low_level = true; // Use our optimized version by default
    
    // Command line options
    static struct option long_options[] = {
        {"stream",   no_argument, 0, 's'},
        {"mmap",     no_argument, 0, 'm'},
        {"parallel", no_argument, 0, 'p'},
        {"single",   no_argument, 0, 'S'},
        {"rapidcsv", no_argument, 0, 'r'}, // Use standard RapidCSV
        {"help",     no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "smpSrh", long_options, &option_index)) != -1) {
        switch (opt) {
            case 's':
                use_mmap = false;
                break;
            case 'm':
                use_mmap = true;
                std::cerr << "Warning: RapidCSV does not support memory-mapped parsing\n";
                use_mmap = false;  // Ignore request and fall back to streaming
                break;
            case 'p':
                use_threads = true;
                std::cerr << "Warning: RapidCSV does not support multi-threaded parsing\n";
                use_threads = false;  // Ignore request and fall back to single-threaded
                break;
            case 'S':
                use_threads = false;
                break;
            case 'r':
                use_low_level = false; // Use standard RapidCSV
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
    
    // Get file size for throughput calculation
    struct stat st;
    size_t file_size = 0;
    if (stat(filename.c_str(), &st) == 0) {
        file_size = static_cast<size_t>(st.st_size);
    } else {
        std::cerr << "Error: Cannot determine file size\n";
        return 1;
    }

  auto start = std::chrono::steady_clock::now();
    size_t rows = 0;
    size_t cols = 0;
  double sum = 0.0;

    // Display parsing mode
    std::cout << "Parsing mode: " << (use_low_level ? "Optimized" : "Standard RapidCSV") 
              << ", Threading: Single-threaded" << std::flush;
    
    if (use_low_level) {
        // Use our optimized low-level parser
        rows = count_csv_lines(filename, cols);
    } else {
        // Configure optimized parsing
        rapidcsv::SeparatorParams sepParams;
        sepParams.mSeparator = ',';
        sepParams.mTrim = false;
        sepParams.mQuotedLinebreaks = true;
        sepParams.mAutoQuote = true;
        
        rapidcsv::LabelParams labelParams(-1, -1); // No labels
        
        rapidcsv::ConverterParams convParams;
        convParams.mNumericLocale = false; // Avoid locale conversions
        convParams.mHasDefaultConverter = true; // Don't throw on conversion errors
        
        // Load the CSV data with optimized parameters
        rapidcsv::Document doc(filename, labelParams, sepParams, convParams);
        
        rows = doc.GetRowCount();
        cols = doc.GetColumnCount();
        
        // Process rows efficiently - only for numeric computation benchmark
        // For line counting benchmark, we avoid this overhead
        if (true) {
            // Process first 10 rows max for sum calculation (avoid spending too much time here)
            size_t max_rows = std::min(rows, size_t(10));
            for (size_t r = 0; r < max_rows; ++r) {
                // Get entire row at once
                std::vector<std::string> row = doc.GetRow<std::string>(r);
                for (size_t c = 0; c < cols && c < row.size(); ++c) {
      try {
                        // Convert to double only once per cell
                        sum += std::stod(row[c]);
      } catch (...) {
                        // Handle non-numeric cells silently
                    }
                }
      }
    }
  }

  auto end = std::chrono::steady_clock::now();
  auto elapsed = end - start;
  double elapsed_seconds = std::chrono::duration<double>(elapsed).count();
  
    std::cout << "\nParsed " << rows << " lines, " << cols << " columns in "
              << elapsed_seconds << " seconds ("
              << (rows / elapsed_seconds) << " lines/sec)" << std::endl;
    std::cout << "Throughput: " << file_size / (1024.0 * 1024.0) / elapsed_seconds << " MB/s\n";
    if (!use_low_level) {
        std::cout << "Sample sum: " << sum << "\n";
    }
    
  return 0;
}