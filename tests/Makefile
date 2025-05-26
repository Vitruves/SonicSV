# Makefile for SonicCSV with SIMD detection and fallback
# Supports systems with and without AVX2/SSE4.2

# Project configuration
PROJECT_NAME = SonicCSV
VERSION = 2.0.0
PREFIX ?= /usr/local

# Directories
SRC_DIR = src
INCLUDE_DIR = include
TEST_DIR = tests
BUILD_DIR = build
DIST_DIR = dist

# Compiler configuration
CC = gcc
CXX = g++
AR = ar
INSTALL = install

# Base compiler flags
CFLAGS_BASE = -std=c99 -Wall -Wextra -Wpedantic -O3 -fPIC
CXXFLAGS_BASE = -std=c++11 -Wall -Wextra -Wpedantic -O3 -fPIC
LDFLAGS_BASE = -lm

# Debug flags
DEBUG_FLAGS = -g -O0 -DDEBUG -fsanitize=address -fsanitize=undefined
RELEASE_FLAGS = -DNDEBUG -flto

# Platform detection
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

# Architecture-specific settings
ifeq ($(UNAME_M),x86_64)
    ARCH_FLAGS = -march=x86-64 -mtune=generic
    HAS_X86 = 1
else ifeq ($(UNAME_M),aarch64)
    ARCH_FLAGS = -march=armv8-a -mtune=generic
    HAS_ARM = 1
else ifeq ($(UNAME_M),arm64)
    ARCH_FLAGS = -march=armv8-a -mtune=generic
    HAS_ARM = 1
else
    ARCH_FLAGS = -mtune=generic
    HAS_GENERIC = 1
endif

# Platform-specific settings
ifeq ($(UNAME_S),Linux)
    PLATFORM_FLAGS = -D_GNU_SOURCE -pthread
    SHARED_EXT = .so
    LDFLAGS_SHARED = -shared -Wl,-soname,lib$(PROJECT_NAME)$(SHARED_EXT).$(VERSION)
else ifeq ($(UNAME_S),Darwin)
    PLATFORM_FLAGS = -D_DARWIN_C_SOURCE -pthread
    SHARED_EXT = .dylib
    LDFLAGS_SHARED = -shared -install_name @rpath/lib$(PROJECT_NAME)$(SHARED_EXT)
    # Apple Silicon specific
    ifeq ($(UNAME_M),arm64)
        ARCH_FLAGS = -mcpu=apple-m1 -mtune=apple-m1
    endif
else
    PLATFORM_FLAGS = -pthread
    SHARED_EXT = .so
    LDFLAGS_SHARED = -shared
endif

# SIMD capability detection at compile time
ifdef HAS_X86
    # Check if compiler supports builtin CPU detection
    GCC_VERSION := $(shell $(CC) -dumpversion | cut -d. -f1)
    GCC_MAJOR_GTE_5 := $(shell [ $(GCC_VERSION) -ge 5 ] && echo true)
    
    # Runtime SIMD detection flags
    SIMD_DETECT_FLAGS = -DHAVE_BUILTIN_CPU_SUPPORTS
    
    # Individual SIMD compilation units
    SIMD_SSE42_FLAGS = -msse4.2 -DHAVE_SSE4_2
    SIMD_AVX2_FLAGS = -mavx2 -DHAVE_AVX2 -DHAVE_SSE4_2
    
    # Conservative baseline for compatibility
    SIMD_BASELINE_FLAGS = -msse2
    
    CFLAGS_SIMD = $(SIMD_DETECT_FLAGS)
else ifdef HAS_ARM
    SIMD_NEON_FLAGS = -DHAVE_NEON
    CFLAGS_SIMD = $(SIMD_NEON_FLAGS)
else
    CFLAGS_SIMD = -DCSV_SIMD_NONE
endif

# Complete flags
CFLAGS = $(CFLAGS_BASE) $(