# SonicSV Makefile
# Simple build system for testing, installation, and CPU detection

# Compiler and flags
CC = gcc
CFLAGS = -std=c99 -O3 -Wall -Wextra -march=native
LDFLAGS = -lpthread

# Installation paths
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
INCLUDEDIR = $(PREFIX)/include

# Source files
HEADER = sonicsv.h
TEST_SRC = tests/sonicsv_test.c
EXAMPLE_SRC = example/example.c

# Binaries
TEST_BIN = tests/sonicsv_test
EXAMPLE_BIN = example/example
CPU_DETECT_BIN = cpu_detect

# Default target
.PHONY: all test example cpu-detect install uninstall clean help
all: test example

# Build test executable
test: $(TEST_BIN)
	@echo "Running tests..."
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC) $(HEADER)
	@echo "Building test executable..."
	$(CC) $(CFLAGS) -DSONICSV_IMPLEMENTATION -o $(TEST_BIN) $(TEST_SRC) $(LDFLAGS)

# Build example executable
example: $(EXAMPLE_BIN)

$(EXAMPLE_BIN): $(EXAMPLE_SRC) $(HEADER)
	@echo "Building example..."
	$(CC) $(CFLAGS) -DSONICSV_IMPLEMENTATION -o $(EXAMPLE_BIN) $(EXAMPLE_SRC) $(LDFLAGS)

# CPU detection utility
cpu-detect: $(CPU_DETECT_BIN)
	@echo "Detecting CPU features..."
	./$(CPU_DETECT_BIN)

$(CPU_DETECT_BIN): cpu_detect.c $(HEADER)
	@echo "Building CPU detection utility..."
	$(CC) $(CFLAGS) -DSONICSV_IMPLEMENTATION -o $(CPU_DETECT_BIN) cpu_detect.c $(LDFLAGS)

cpu_detect.c:
	@echo "Creating CPU detection utility..."
	@echo '#define SONICSV_IMPLEMENTATION' > cpu_detect.c
	@echo '#include "sonicsv.h"' >> cpu_detect.c
	@echo '#include <stdio.h>' >> cpu_detect.c
	@echo '' >> cpu_detect.c
	@echo 'int main() {' >> cpu_detect.c
	@echo '    printf("SonicSV CPU Feature Detection\\n");' >> cpu_detect.c
	@echo '    printf("============================\\n");' >> cpu_detect.c
	@echo '    ' >> cpu_detect.c
	@echo '    uint32_t features = csv_get_simd_features();' >> cpu_detect.c
	@echo '    ' >> cpu_detect.c
	@echo '    printf("Detected SIMD features:\\n");' >> cpu_detect.c
	@echo '    if (features == CSV_SIMD_NONE) {' >> cpu_detect.c
	@echo '        printf("  - None (scalar fallback)\\n");' >> cpu_detect.c
	@echo '    } else {' >> cpu_detect.c
	@echo '        if (features & CSV_SIMD_SSE4_2) printf("  - SSE4.2\\n");' >> cpu_detect.c
	@echo '        if (features & CSV_SIMD_AVX2) printf("  - AVX2\\n");' >> cpu_detect.c
	@echo '        if (features & CSV_SIMD_AVX512) printf("  - AVX-512\\n");' >> cpu_detect.c
	@echo '        if (features & CSV_SIMD_NEON) printf("  - NEON\\n");' >> cpu_detect.c
	@echo '        if (features & CSV_SIMD_SVE) printf("  - SVE\\n");' >> cpu_detect.c
	@echo '    }' >> cpu_detect.c
	@echo '    ' >> cpu_detect.c
	@echo '    printf("\\nArchitecture info:\\n");' >> cpu_detect.c
	@echo '#ifdef __x86_64__' >> cpu_detect.c
	@echo '    printf("  - Architecture: x86_64\\n");' >> cpu_detect.c
	@echo '#elif defined(__aarch64__)' >> cpu_detect.c
	@echo '    printf("  - Architecture: ARM64\\n");' >> cpu_detect.c
	@echo '#elif defined(__arm__)' >> cpu_detect.c
	@echo '    printf("  - Architecture: ARM32\\n");' >> cpu_detect.c
	@echo '#else' >> cpu_detect.c
	@echo '    printf("  - Architecture: Other\\n");' >> cpu_detect.c
	@echo '#endif' >> cpu_detect.c
	@echo '    ' >> cpu_detect.c
	@echo '#ifdef __GNUC__' >> cpu_detect.c
	@echo '    printf("  - Compiler: GCC %d.%d.%d\\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);' >> cpu_detect.c
	@echo '#elif defined(__clang__)' >> cpu_detect.c
	@echo '    printf("  - Compiler: Clang\\n");' >> cpu_detect.c
	@echo '#else' >> cpu_detect.c
	@echo '    printf("  - Compiler: Unknown\\n");' >> cpu_detect.c
	@echo '#endif' >> cpu_detect.c
	@echo '    ' >> cpu_detect.c
	@echo '    printf("\\nSIMD alignment: %d bytes\\n", SONICSV_SIMD_ALIGN);' >> cpu_detect.c
	@echo '    printf("Cache line size: %d bytes\\n", SONICSV_CACHE_LINE_SIZE);' >> cpu_detect.c
	@echo '    ' >> cpu_detect.c
	@echo '    return 0;' >> cpu_detect.c
	@echo '}' >> cpu_detect.c

# Install header file
install: $(HEADER)
	@echo "Installing SonicSV to $(PREFIX)..."
	@mkdir -p $(INCLUDEDIR)
	@cp $(HEADER) $(INCLUDEDIR)/
	@echo "Header installed to $(INCLUDEDIR)/$(HEADER)"
	@echo "Include in your projects with: #include <sonicsv.h>"

# Uninstall
uninstall:
	@echo "Uninstalling SonicSV from $(PREFIX)..."
	@rm -f $(INCLUDEDIR)/$(HEADER)
	@echo "SonicSV uninstalled"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -f $(TEST_BIN) $(EXAMPLE_BIN) $(CPU_DETECT_BIN) cpu_detect.c
	@echo "Clean complete"

# Help
help:
	@echo "SonicSV Makefile targets:"
	@echo "  all        - Build test and example (default)"
	@echo "  test       - Build and run tests"
	@echo "  example    - Build example program"
	@echo "  cpu-detect - Build and run CPU feature detection"
	@echo "  install    - Install header to system (PREFIX=$(PREFIX))"
	@echo "  uninstall  - Remove header from system"
	@echo "  clean      - Remove build artifacts"
	@echo "  help       - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make test                    # Run tests"
	@echo "  make cpu-detect              # Check CPU features"
	@echo "  make install                 # Install to /usr/local"
	@echo "  make install PREFIX=/opt     # Install to /opt"