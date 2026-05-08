# SonicSV Makefile

CC = gcc
CXX ?= c++
CFLAGS = -std=c11 -O3 -Wall -Wextra -march=native -DSONICSV_IMPLEMENTATION
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -DSONICSV_IMPLEMENTATION
LDFLAGS = -lpthread -lm

# POSIX feature-test macros. These have to be defined BEFORE any system
# header is included, so passing them as compiler flags is the reliable
# spelling — the matching #defines inside sonicsv.h are a fallback for
# users who control include order. _POSIX_C_SOURCE=200809L unlocks
# clock_gettime, posix_madvise, etc.; _DEFAULT_SOURCE keeps glibc's
# madvise/MADV_* visible alongside POSIX. `override` is required so these
# survive a command-line `make CFLAGS=...` invocation (CI does this) —
# without it, the user-supplied CFLAGS clobbers all subsequent `+=`.
ifneq ($(OS),Windows_NT)
override CFLAGS += -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -DSONICSV_IMPLEMENTATION
override CXXFLAGS += -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -DSONICSV_IMPLEMENTATION
endif

# On macOS, match the deployment target to the running system so we don't
# emit linker warnings when linking Homebrew dylibs (libcsv etc.) that were
# built against a newer SDK.
ifeq ($(shell uname -s),Darwin)
MACOS_VERSION := $(shell sw_vers -productVersion | cut -d. -f1)
CFLAGS += -mmacosx-version-min=$(MACOS_VERSION).0
CXXFLAGS += -mmacosx-version-min=$(MACOS_VERSION).0
endif

# Installation paths
PREFIX ?= /usr/local
INSTALL_INCLUDE_DIR = $(PREFIX)/include

# Directories
BUILD_DIR = build
TEST_DIR = tests
BENCH_DIR = benchmark
EXAMPLE_DIR = example

# Targets
TEST_BIN = $(BUILD_DIR)/sonicsv_test
TEST_CPP_BIN = $(BUILD_DIR)/sonicsv_test_cpp
INCLUDE_ORDER_BIN = $(BUILD_DIR)/include_order_smoke
BENCH_BIN = $(BUILD_DIR)/benchmark_suite
EXAMPLE_BIN = $(BUILD_DIR)/example

.PHONY: all test benchmark example install uninstall clean help

all: test

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build and run tests. The C++ smoke binary and the include-order smoke
# binary are built here but launched from inside the C test runner
# (test_cpp_smoke_external / test_include_order_smoke_external), so they
# both show up in the regular test count.
test: $(TEST_BIN) $(TEST_CPP_BIN) $(INCLUDE_ORDER_BIN)
	@./$(TEST_BIN)

# Smoke test: standard headers included BEFORE sonicsv.h. Catches missing
# POSIX feature-test-macro flags (must be passed as -D since header-local
# defines arrive too late once <stdio.h> has pulled in <features.h>).
$(INCLUDE_ORDER_BIN): $(TEST_DIR)/include_order_smoke.c sonicsv.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Wpedantic -o $@ $(TEST_DIR)/include_order_smoke.c $(LDFLAGS)

$(TEST_BIN): $(TEST_DIR)/sonicsv_test.c sonicsv.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_DIR)/sonicsv_test.c $(LDFLAGS)

TEST_CPP_IMPL_OBJ = $(BUILD_DIR)/sonicsv_impl.o

$(TEST_CPP_IMPL_OBJ): $(TEST_DIR)/sonicsv_impl.c sonicsv.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $(TEST_DIR)/sonicsv_impl.c -o $@

$(TEST_CPP_BIN): $(TEST_DIR)/sonicsv_test.cpp $(TEST_CPP_IMPL_OBJ) sonicsv.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_DIR)/sonicsv_test.cpp $(TEST_CPP_IMPL_OBJ) $(LDFLAGS)

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

# Install header to system
install: sonicsv.h
	@echo "Installing sonicsv.h to $(INSTALL_INCLUDE_DIR)/"
	@mkdir -p $(INSTALL_INCLUDE_DIR)
	@install -m 644 sonicsv.h $(INSTALL_INCLUDE_DIR)/sonicsv.h
	@echo "Installation complete. Include with: #include <sonicsv.h>"

# Uninstall header from system
uninstall:
	@echo "Removing sonicsv.h from $(INSTALL_INCLUDE_DIR)/"
	@rm -f $(INSTALL_INCLUDE_DIR)/sonicsv.h
	@echo "Uninstallation complete."

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "SonicSV Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make test       - Build and run the test suite"
	@echo "  make benchmark  - Build and run benchmarks (requires libcsv)"
	@echo "  make example    - Build and run the example program"
	@echo "  make install    - Install header to system (default: /usr/local/include)"
	@echo "  make uninstall  - Remove header from system"
	@echo "  make clean      - Remove build artifacts"
	@echo "  make help       - Show this help message"
	@echo ""
	@echo "Installation options:"
	@echo "  make install PREFIX=/custom/path  - Install to custom location"
	@echo ""
	@echo "Prerequisites for benchmark:"
	@echo "  macOS:  brew install libcsv"
	@echo "  Linux:  apt install libcsv-dev"
