CC = gcc
CFLAGS = -Wall -Wextra -O3 -std=c99 -fPIC
LDFLAGS = -lm -lcsv

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

# Source files
SRCS = sonicsv.c
OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))
LIBS = $(LIB_DIR)/libsonicsv.so

# Benchmark programs
BENCH_SRC = benchmark/sonicsv_bench.c
BENCH_OBJ = $(BUILD_DIR)/benchmark/sonicsv_bench.o $(BUILD_DIR)/benchmark/libcsv_wrapper.o
BENCH_PROG = $(BIN_DIR)/sonicsv_bench

# Main targets
all: directories $(LIBS) $(BENCH_PROG)

# Create necessary directories
directories:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR) $(LIB_DIR) $(BUILD_DIR)/benchmark

# Shared libraries
$(LIB_DIR)/libsonicsv.so: $(BUILD_DIR)/sonicsv.o
	$(CC) -shared -o $@ $^ $(LDFLAGS)

# Object files
$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/benchmark/%.o: benchmark/%.c
	$(CC) $(CFLAGS) -c $< -o $@ -I.

# Benchmark program
$(BENCH_PROG): $(BUILD_DIR)/benchmark/sonicsv_bench.o $(BUILD_DIR)/benchmark/libcsv_wrapper.o $(BUILD_DIR)/sonicsv.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Run benchmark
benchmark: $(BENCH_PROG)
	$(BENCH_PROG) benchmark_data.csv

# Clean build files
clean:
	rm -rf $(BUILD_DIR)/* $(BIN_DIR)/* $(LIB_DIR)/*

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
	@echo "  all             - Build libraries and benchmarks"
	@echo "  clean           - Remove build files"
	@echo "  compile-commands - Generate compile_commands.json for IDE integration"
	@echo "  release         - Build release version (optimized, no debug info)"
	@echo "  benchmark       - Run the benchmark program"
	@echo "  install         - Install libraries and headers"

.PHONY: all clean benchmark install directories help compile-commands release