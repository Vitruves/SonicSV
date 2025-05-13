CC = gcc
CFLAGS = -Wall -Wextra -O3 -std=c99 -fPIC
LDFLAGS = -lm -lcsv -pthread

# Detect if we're on a system with SIMD extensions
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

# Add architecture-specific flags
ifeq ($(UNAME_M),x86_64)
    CFLAGS += -march=native
    # Check for SSE/AVX
    ifeq ($(shell grep -o avx2 /proc/cpuinfo | head -1),avx2)
        CFLAGS += -mavx2
    endif
    ifeq ($(shell grep -o sse4_2 /proc/cpuinfo | head -1),sse4_2)
        CFLAGS += -msse4.2
    endif
endif

# ARM-specific flags
ifneq ($(filter aarch64 arm64,$(UNAME_M)),)
    CFLAGS += -march=native
    # Enable NEON if available
    ifeq ($(shell grep -o neon /proc/cpuinfo | head -1),neon)
        CFLAGS += -mfpu=neon
    endif
endif

# Output directories
BUILD_DIR = build
BIN_DIR = bin
LIB_DIR = lib
EXAMPLES_DIR = examples
DATA_DIR = data

# Source files
SRCS = sonicsv.c
OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))
LIBS = $(LIB_DIR)/libsonicsv.so

# Benchmark programs
BENCH_SRC = benchmark/sonicsv_bench.c
BENCH_OBJ = $(BUILD_DIR)/benchmark/sonicsv_bench.o
BENCH_PROG = $(BIN_DIR)/sonicsv_bench

# Example programs - filter only existing files
EXAMPLE_SOURCES := $(wildcard $(EXAMPLES_DIR)/*.c)
EXAMPLE_PROGS := $(patsubst $(EXAMPLES_DIR)/%.c,$(BIN_DIR)/%,$(EXAMPLE_SOURCES))

# Main targets
all: directories $(LIBS) $(BENCH_PROG) examples

# Create necessary directories
directories:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR) $(LIB_DIR) $(BUILD_DIR)/benchmark $(EXAMPLES_DIR) $(DATA_DIR)

# Shared libraries
$(LIB_DIR)/libsonicsv.so: $(BUILD_DIR)/sonicsv.o
	$(CC) -shared -o $@ $^ $(LDFLAGS)

# Object files
$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/benchmark/%.o: benchmark/%.c
	$(CC) $(CFLAGS) -c $< -o $@ -I.

# Benchmark program - updated to not depend on libcsv_wrapper.o
$(BENCH_PROG): $(BUILD_DIR)/benchmark/sonicsv_bench.o $(BUILD_DIR)/sonicsv.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Examples
examples: directories $(LIBS) $(EXAMPLE_PROGS)

$(BIN_DIR)/%: $(EXAMPLES_DIR)/%.c $(LIBS)
	$(CC) $(CFLAGS) -o $@ $< -I. -L$(LIB_DIR) -lsonicsv -pthread -Wl,-rpath,'$$ORIGIN/../lib'

# Run an example with the correct library path
run-example: examples
	@echo "Available examples:"
	@for example in $(notdir $(EXAMPLE_PROGS)); do \
		echo "  $$example"; \
	done
	@echo ""
	@read -p "Enter example name to run: " example; \
	if [ -f "$(BIN_DIR)/$$example" ]; then \
		echo "Running $$example..."; \
		echo ""; \
		LD_LIBRARY_PATH="$(shell pwd)/$(LIB_DIR)" $(BIN_DIR)/$$example $(ARGS); \
	else \
		echo "Example '$$example' not found"; \
	fi

# Run specific example with arguments
run-%: $(BIN_DIR)/%
	@echo "Running $*..."
	@echo ""
	@LD_LIBRARY_PATH="$(shell pwd)/$(LIB_DIR)" $(BIN_DIR)/$* $(ARGS)

# Get CSV sample from the internet if needed
get-sample-data:
	@mkdir -p $(DATA_DIR)
	@if [ ! -f "$(DATA_DIR)/sample.csv" ]; then \
		echo "Downloading sample CSV data..."; \
		curl -s -o $(DATA_DIR)/sample.csv https://raw.githubusercontent.com/datasets/covid-19/master/data/time-series-19-covid-combined.csv || \
		wget -q -O $(DATA_DIR)/sample.csv https://raw.githubusercontent.com/datasets/covid-19/master/data/time-series-19-covid-combined.csv; \
		echo "Sample data downloaded to $(DATA_DIR)/sample.csv"; \
	else \
		echo "Sample data already exists at $(DATA_DIR)/sample.csv"; \
	fi

# Run multithreading example with sample data
run-multithreading-demo: $(BIN_DIR)/multithreading get-sample-data
	@echo "Running multithreading example with sample data..."
	@LD_LIBRARY_PATH="$(shell pwd)/$(LIB_DIR)" $(BIN_DIR)/multithreading $(DATA_DIR)/sample.csv

# Run benchmark with sample data
benchmark: $(BENCH_PROG) get-sample-data
	@echo "Running benchmark with sample data..."
	@LD_LIBRARY_PATH="$(shell pwd)/$(LIB_DIR)" $(BENCH_PROG) -i $(DATA_DIR)/sample.csv

# Clean build files
clean:
	rm -rf $(BUILD_DIR)/* $(BIN_DIR)/* $(LIB_DIR)/*

# Clean everything including downloaded data
distclean: clean
	rm -rf $(DATA_DIR)/*

# Generate compile commands for IDE integration
compile-commands:
	compiledb make clean all

# Release build
release: CFLAGS += -DNDEBUG
release: clean all

# Install libraries and headers
install: all
	@mkdir -p /usr/local/include/sonicsv
	@mkdir -p /usr/local/lib
	cp *.h /usr/local/include/sonicsv/
	cp $(LIB_DIR)/* /usr/local/lib/
	ldconfig

# Show help information
help:
	@echo "Available targets:"
	@echo "  all                 - Build libraries, benchmarks, and examples"
	@echo "  examples            - Build example programs"
	@echo "  run-example         - Run an example program interactively"
	@echo "  run-EXAMPLE         - Run a specific example (e.g., run-simple_parsing)"
	@echo "  run-multithreading-demo - Run multithreading example with sample data"
	@echo "  get-sample-data     - Download sample CSV data for examples"
	@echo "  clean               - Remove build files"
	@echo "  distclean           - Remove build files and downloaded data"
	@echo "  compile-commands    - Generate compile_commands.json for IDE integration"
	@echo "  release             - Build release version (optimized, no debug info)"
	@echo "  benchmark           - Run the benchmark program with sample data"
	@echo "  install             - Install libraries and headers"
	@echo ""
	@echo "Parameters:"
	@echo "  ARGS                - Arguments to pass to examples (e.g., make run-simple_parsing ARGS='data.csv')"

.PHONY: all clean distclean benchmark install directories help compile-commands release examples run-example get-sample-data run-multithreading-demo