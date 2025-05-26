# SonicSV - Fast SIMD-Accelerated CSV Parser
# Minimal Makefile for header installation with SIMD detection

# Installation directories
PREFIX ?= /usr/local
INSTALL_INCLUDE_DIR = $(PREFIX)/include

# Header file
HEADER_FILE = include/sonicsv.h

# Colors for output
RED = \033[0;31m
GREEN = \033[0;32m
YELLOW = \033[0;33m
BLUE = \033[0;34m
NC = \033[0m

# SIMD detection variables
ARCH := $(shell uname -m)
CPU_INFO := /proc/cpuinfo

# Default target
.PHONY: all
all: detect-simd install

# Detect SIMD capabilities
.PHONY: detect-simd
detect-simd:
	@echo "$(BLUE)-i-- Detecting SIMD capabilities$(NC)"
	@echo "$(BLUE)-i-- Architecture: $(ARCH)$(NC)"
ifeq ($(ARCH),x86_64)
	@echo "$(BLUE)-i-- Checking x86_64 SIMD features$(NC)"
	@if grep -q "sse4_2" $(CPU_INFO); then \
		echo "$(GREEN)-s-- SSE4.2 support detected$(NC)"; \
	else \
		echo "$(YELLOW)-w-- SSE4.2 not detected$(NC)"; \
	fi
	@if grep -q "avx2" $(CPU_INFO); then \
		echo "$(GREEN)-s-- AVX2 support detected$(NC)"; \
	else \
		echo "$(YELLOW)-w-- AVX2 not detected$(NC)"; \
	fi
	@if grep -q "avx512" $(CPU_INFO); then \
		echo "$(GREEN)-s-- AVX512 support detected$(NC)"; \
	else \
		echo "$(BLUE)-i-- AVX512 not detected (not required)$(NC)"; \
	fi
else ifeq ($(ARCH),aarch64)
	@echo "$(BLUE)-i-- Checking ARM64 SIMD features$(NC)"
	@if grep -q "asimd" $(CPU_INFO) || grep -q "neon" $(CPU_INFO); then \
		echo "$(GREEN)-s-- NEON support detected$(NC)"; \
	else \
		echo "$(YELLOW)-w-- NEON not detected$(NC)"; \
	fi
else
	@echo "$(YELLOW)-w-- Architecture $(ARCH) - SIMD detection limited$(NC)"
	@echo "$(BLUE)-i-- SonicSV will fall back to scalar implementation$(NC)"
endif
	@echo "$(GREEN)-s-- SIMD detection completed$(NC)"

# Install header
.PHONY: install
install: $(HEADER_FILE)
	@echo "$(BLUE)-i-- Installing SonicSV header$(NC)"
	@if [ ! -f "$(HEADER_FILE)" ]; then \
		echo "$(RED)-e-- Header file $(HEADER_FILE) not found$(NC)"; \
		exit 1; \
	fi
	@mkdir -p $(INSTALL_INCLUDE_DIR)
	@cp $(HEADER_FILE) $(INSTALL_INCLUDE_DIR)/
	@chmod 644 $(INSTALL_INCLUDE_DIR)/sonicsv.h
	@echo "$(GREEN)-s-- SonicSV header installed to $(INSTALL_INCLUDE_DIR)/sonicsv.h$(NC)"

# Uninstall header
.PHONY: uninstall
uninstall:
	@echo "$(BLUE)-i-- Uninstalling SonicSV header$(NC)"
	@if [ -f "$(INSTALL_INCLUDE_DIR)/sonicsv.h" ]; then \
		rm -f $(INSTALL_INCLUDE_DIR)/sonicsv.h; \
		echo "$(GREEN)-s-- SonicSV header uninstalled$(NC)"; \
	else \
		echo "$(YELLOW)-w-- SonicSV header not found at $(INSTALL_INCLUDE_DIR)/sonicsv.h$(NC)"; \
	fi

# Test installation with SIMD detection
.PHONY: test-install
test-install: install
	@echo "$(BLUE)-i-- Testing header installation with SIMD detection$(NC)"
	@echo '#include <stdio.h>' > /tmp/test_sonicsv.c
	@echo '#define SONICSV_IMPLEMENTATION' >> /tmp/test_sonicsv.c
	@echo '#include <sonicsv.h>' >> /tmp/test_sonicsv.c
	@echo 'int main() {' >> /tmp/test_sonicsv.c
	@echo '    csv_simd_init();' >> /tmp/test_sonicsv.c
	@echo '    uint32_t features = csv_simd_get_features();' >> /tmp/test_sonicsv.c
	@echo '    printf("SIMD features detected: 0x%x\\n", features);' >> /tmp/test_sonicsv.c
	@echo '    if (features & CSV_SIMD_SSE4_2) printf("  - SSE4.2\\n");' >> /tmp/test_sonicsv.c
	@echo '    if (features & CSV_SIMD_AVX2) printf("  - AVX2\\n");' >> /tmp/test_sonicsv.c
	@echo '    if (features & CSV_SIMD_NEON) printf("  - NEON\\n");' >> /tmp/test_sonicsv.c
	@echo '    if (features == CSV_SIMD_NONE) printf("  - Scalar only\\n");' >> /tmp/test_sonicsv.c
	@echo '    return 0;' >> /tmp/test_sonicsv.c
	@echo '}' >> /tmp/test_sonicsv.c
	@if gcc -O2 -march=native -std=c99 /tmp/test_sonicsv.c -o /tmp/test_sonicsv -lm -lpthread 2>/dev/null; then \
		echo "$(GREEN)-s-- Header compilation test passed$(NC)"; \
		echo "$(BLUE)-i-- Running SIMD feature detection:$(NC)"; \
		/tmp/test_sonicsv; \
		echo "$(GREEN)-s-- Header installation test completed$(NC)"; \
	else \
		echo "$(RED)-e-- Header compilation test failed$(NC)"; \
		exit 1; \
	fi
	@rm -f /tmp/test_sonicsv.c /tmp/test_sonicsv

# Show installation status
.PHONY: status
status:
	@echo "$(BLUE)-i-- SonicSV Installation Status$(NC)"
	@echo "$(YELLOW)Installation directory:$(NC) $(INSTALL_INCLUDE_DIR)"
	@if [ -f "$(INSTALL_INCLUDE_DIR)/sonicsv.h" ]; then \
		echo "$(GREEN)-s-- SonicSV header is installed$(NC)"; \
		echo "$(BLUE)-i-- File: $(INSTALL_INCLUDE_DIR)/sonicsv.h$(NC)"; \
		echo "$(BLUE)-i-- Size: $$(stat -c%s $(INSTALL_INCLUDE_DIR)/sonicsv.h 2>/dev/null || echo 'unknown') bytes$(NC)"; \
	else \
		echo "$(YELLOW)-w-- SonicSV header is not installed$(NC)"; \
	fi

# Help target
.PHONY: help
help:
	@echo "$(BLUE)SonicSV - Fast SIMD-Accelerated CSV Parser$(NC)"
	@echo "$(BLUE)Header-only library installation$(NC)"
	@echo ""
	@echo "$(YELLOW)Available targets:$(NC)"
	@echo "  $(GREEN)install$(NC)       - Install SonicSV header to $(INSTALL_INCLUDE_DIR)"
	@echo "  $(GREEN)uninstall$(NC)     - Remove installed SonicSV header"
	@echo "  $(GREEN)detect-simd$(NC)   - Detect available SIMD features"
	@echo "  $(GREEN)test-install$(NC)  - Test header installation and SIMD detection"
	@echo "  $(GREEN)status$(NC)        - Show installation status"
	@echo "  $(GREEN)help$(NC)          - Show this help message"
	@echo ""
	@echo "$(YELLOW)Environment variables:$(NC)"
	@echo "  PREFIX        - Installation prefix (default: /usr/local)"
	@echo ""
	@echo "$(YELLOW)Examples:$(NC)"
	@echo "  make install                    # Install to /usr/local/include"
	@echo "  make PREFIX=/opt install        # Install to /opt/include"
	@echo "  make detect-simd               # Check SIMD capabilities"
	@echo "  make test-install              # Test installation with SIMD"
	@echo ""
	@echo "$(YELLOW)SIMD Support:$(NC)"
	@echo "  x86_64: SSE4.2, AVX2 (runtime detection)"
	@echo "  ARM64:  NEON (compile-time detection)"
	@echo "  Other:  Scalar fallback"
	@echo ""
