# SonicSV Makefile

CC = gcc
CFLAGS = -std=c99 -O3 -Wall -Wextra -march=native -DSONICSV_IMPLEMENTATION
LDFLAGS = -lpthread -lm

# Directories
BUILD_DIR = build
TEST_DIR = tests
BENCH_DIR = benchmark
EXAMPLE_DIR = example

# Targets
TEST_BIN = $(BUILD_DIR)/sonicsv_test
BENCH_BIN = $(BUILD_DIR)/benchmark_suite
EXAMPLE_BIN = $(BUILD_DIR)/example

.PHONY: all test benchmark example clean help

all: test

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build and run tests
test: $(TEST_BIN)
	@echo "Running tests..."
	@./$(TEST_BIN)

$(TEST_BIN): $(TEST_DIR)/sonicsv_test.c sonicsv.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_DIR)/sonicsv_test.c $(LDFLAGS)

# Build and run benchmark (requires libcsv)
benchmark: $(BENCH_BIN)
	@echo "Running benchmark..."
	@./$(BENCH_BIN)

$(BENCH_BIN): $(BENCH_DIR)/benchmark_suite.c sonicsv.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(BENCH_DIR)/benchmark_suite.c -lcsv $(LDFLAGS)

# Build and run example
example: $(EXAMPLE_BIN)
	@echo "Running example..."
	@./$(EXAMPLE_BIN)

$(EXAMPLE_BIN): $(EXAMPLE_DIR)/example.c sonicsv.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(EXAMPLE_DIR)/example.c $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "SonicSV Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make test       - Build and run the test suite"
	@echo "  make benchmark  - Build and run benchmarks (requires libcsv)"
	@echo "  make example    - Build and run the example program"
	@echo "  make clean      - Remove build artifacts"
	@echo "  make help       - Show this help message"
	@echo ""
	@echo "Prerequisites for benchmark:"
	@echo "  macOS:  brew install libcsv"
	@echo "  Linux:  apt install libcsv-dev"
