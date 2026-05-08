/*
 * ============================================================================
 * SonicSV - Ultra-Fast SIMD-Accelerated CSV Parser
 * ============================================================================
 * Version: 3.2.1 | Single-header | C11 | MIT License
 *
 * A blazing-fast CSV parser that automatically uses SIMD instructions
 * (SSE4.2, AVX2, AVX-512, NEON) for maximum performance. Achieves
 * up to 6 GB/s parsing speed on modern hardware.
 *
 * Copyright (c) 2025 JHG Natter

 * ============================================================================
 * LICENSE (MIT)
 * ============================================================================
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * ============================================================================
 *
 * QUICK START
 * ============================================================================
 *
 * In exactly ONE .c file, define SONICSV_IMPLEMENTATION before including:
 *
 *     #define SONICSV_IMPLEMENTATION
 *     #include "sonicsv.h"
 *
 * Other files just `#include "sonicsv.h"`.
 *
 * On POSIX systems, build with `-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE`
 * so clock_gettime / posix_madvise / madvise / MADV_* are visible. The
 * header has matching #defines as a fallback, but they have to be visible
 * before any system header — passing them on the command line is the
 * reliable spelling.
 *
 * Minimal usage — parse a file, print every field:
 *
 *     static void on_row(const csv_row_t *row, void *ctx) {
 *         (void)ctx;
 *         for (size_t i = 0; i < row->num_fields; i++) {
 *             const csv_field_t *f = csv_get_field(row, i);
 *             printf("%.*s%c", (int)f->size, f->data,
 *                    i + 1 == row->num_fields ? '\n' : '\t');
 *         }
 *     }
 *
 *     int main(void) {
 *         csv_parser_t *p = csv_parser_create(NULL);
 *         csv_parser_set_row_callback(p, on_row, NULL);
 *         csv_parse_file(p, "data.csv");
 *         csv_parser_destroy(p);
 *     }
 *
 * Field data is NOT null-terminated — always use (f->data, f->size).
 *
 * Custom options (TSV, strict mode, larger I/O buffer):
 *
 *     csv_parse_options_t opts = csv_default_options();
 *     opts.delimiter   = '\t';
 *     opts.strict_mode = true;
 *     opts.buffer_size = 256 * 1024;
 *     csv_parser_t *p  = csv_parser_create(&opts);
 *
 * Streaming a FILE* (chunks, no full load):
 *
 *     FILE *fp = fopen("huge.csv", "rb");
 *     csv_parse_stream(p, fp);
 *     fclose(fp);
 *
 * Each csv_parser_t is independent and thread-safe to use concurrently.
 *
 * C++: include the header from .cpp files, but compile the implementation
 * TU as C — the implementation uses C99 compound literals / designated
 * initializers that aren't standard C++. The "implementation TU" is just
 * a one-line .c file:
 *
 *     // sonicsv_impl.c
 *     #define SONICSV_IMPLEMENTATION
 *     #include "sonicsv.h"
 *
 * Compile it with your C compiler and link the resulting object alongside
 * your C++ objects.
 *
 * ============================================================================
 */

 #ifndef SONICSV_H
 #define SONICSV_H

 #if !defined(_WIN32)
 // POSIX 2008 (200809L) is needed for posix_madvise() and other interfaces;
 // on glibc, madvise() / MADV_* additionally require _DEFAULT_SOURCE.
 #  ifndef _POSIX_C_SOURCE
 #  define _POSIX_C_SOURCE 200809L
 #  endif
 #  ifndef _DEFAULT_SOURCE
 #  define _DEFAULT_SOURCE
 #  endif
 #  ifndef _ISOC11_SOURCE
 #  define _ISOC11_SOURCE
 #  endif
 #endif

 #if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
 #  define _CRT_SECURE_NO_WARNINGS
 #endif

 #include <stdbool.h>
 #include <stddef.h>
 #include <stdint.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <time.h>

 #ifdef __cplusplus
 extern "C" {
 #endif
 
 #define SONICSV_VERSION_MAJOR 3
 #define SONICSV_VERSION_MINOR 2
 #define SONICSV_VERSION_PATCH 1
 
 // Enhanced compiler hints and target attributes for better SIMD compilation
 #if defined(__GNUC__) || defined(__clang__)
 #define sonicsv_likely(x)   __builtin_expect(!!(x), 1)
 #define sonicsv_unlikely(x) __builtin_expect(!!(x), 0)
 #define sonicsv_always_inline __attribute__((always_inline)) inline
 #define sonicsv_prefetch_read(ptr, offset) __builtin_prefetch((char*)(ptr) + (offset), 0, 3)
 #define sonicsv_prefetch_write(ptr, offset) __builtin_prefetch((char*)(ptr) + (offset), 1, 3)
 #define sonicsv_assume_aligned(ptr, align) __builtin_assume_aligned(ptr, align)
 #define sonicsv_force_inline __attribute__((always_inline, flatten)) inline
 #define sonicsv_hot __attribute__((hot))
 #define sonicsv_cold __attribute__((cold))
 #define sonicsv_aligned(n) __attribute__((aligned(n)))
 #define SONICSV_TARGET(x) __attribute__((target(x)))
 #define SONICSV_THREAD_LOCAL __thread
 #elif defined(_MSC_VER)
 #define sonicsv_likely(x)   (x)
 #define sonicsv_unlikely(x) (x)
 #define sonicsv_always_inline __forceinline
 #define sonicsv_prefetch_read(ptr, offset) ((void)0)
 #define sonicsv_prefetch_write(ptr, offset) ((void)0)
 #define sonicsv_assume_aligned(ptr, align) (ptr)
 #define sonicsv_force_inline __forceinline
 #define sonicsv_hot
 #define sonicsv_cold
 #define sonicsv_aligned(n) __declspec(align(n))
 // MSVC has no per-function target attribute. The intrinsics compile if the
 // global /arch flag enables them; SONICSV_TARGET() expands to nothing so
 // call sites stay syntactically valid.
 #define SONICSV_TARGET(x)
 #define SONICSV_THREAD_LOCAL __declspec(thread)
 #else
 #define sonicsv_likely(x)   (x)
 #define sonicsv_unlikely(x) (x)
 #define sonicsv_always_inline inline
 #define sonicsv_prefetch_read(ptr, offset) ((void)0)
 #define sonicsv_prefetch_write(ptr, offset) ((void)0)
 #define sonicsv_assume_aligned(ptr, align) (ptr)
 #define sonicsv_force_inline inline
 #define sonicsv_hot
 #define sonicsv_cold
 #define sonicsv_aligned(n)
 #define SONICSV_TARGET(x)
 #if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
 #define SONICSV_THREAD_LOCAL _Thread_local
 #else
 #define SONICSV_THREAD_LOCAL
 #endif
 #endif

 // Portable bit-scan wrappers. GCC/Clang have __builtin_ctz/ctzll/clzll;
 // MSVC has _BitScanForward/_BitScanForward64. We expose sonicsv_ctz32 and
 // sonicsv_ctz64; behavior on a zero argument is undefined (matches the
 // GCC builtins) — every call site already gates on `mask != 0`.
 #if defined(__GNUC__) || defined(__clang__)
 #define sonicsv_ctz32(x) ((int)__builtin_ctz((unsigned int)(x)))
 #define sonicsv_ctz64(x) ((int)__builtin_ctzll((unsigned long long)(x)))
 #define sonicsv_clz64(x) ((int)__builtin_clzll((unsigned long long)(x)))
 #elif defined(_MSC_VER)
 #include <intrin.h>
 static sonicsv_always_inline int sonicsv_ctz32(unsigned int x) {
     unsigned long idx;
     _BitScanForward(&idx, x);
     return (int)idx;
 }
 static sonicsv_always_inline int sonicsv_ctz64(unsigned long long x) {
     unsigned long idx;
 #if defined(_M_X64) || defined(_M_ARM64)
     _BitScanForward64(&idx, x);
 #else
     // 32-bit MSVC: split into halves.
     if ((unsigned int)x) { _BitScanForward(&idx, (unsigned int)x); return (int)idx; }
     _BitScanForward(&idx, (unsigned int)(x >> 32));
     return (int)idx + 32;
 #endif
     return (int)idx;
 }
 static sonicsv_always_inline int sonicsv_clz64(unsigned long long x) {
     unsigned long idx;
 #if defined(_M_X64) || defined(_M_ARM64)
     _BitScanReverse64(&idx, x);
     return 63 - (int)idx;
 #else
     unsigned int hi = (unsigned int)(x >> 32);
     if (hi) { _BitScanReverse(&idx, hi); return 31 - (int)idx; }
     _BitScanReverse(&idx, (unsigned int)x);
     return 63 - (int)idx;
 #endif
 }
 #else
 // Portable fallback (slow). Only hit if neither GCC/Clang nor MSVC.
 static sonicsv_always_inline int sonicsv_ctz32(unsigned int x) {
     int c = 0;
     while (!(x & 1u)) { x >>= 1; c++; }
     return c;
 }
 static sonicsv_always_inline int sonicsv_ctz64(unsigned long long x) {
     int c = 0;
     while (!(x & 1ULL)) { x >>= 1; c++; }
     return c;
 }
 static sonicsv_always_inline int sonicsv_clz64(unsigned long long x) {
     int c = 0;
     while (!(x & (1ULL << 63))) { x <<= 1; c++; }
     return c;
 }
 #endif

 // Cache line size for optimal prefetching
 #define SONICSV_CACHE_LINE_SIZE 64
 #define SONICSV_PREFETCH_DISTANCE (8 * SONICSV_CACHE_LINE_SIZE)
 
 // SIMD feature flags
 #define CSV_SIMD_NONE 0x00
 #define CSV_SIMD_SSE4_2 0x01
 #define CSV_SIMD_AVX2 0x02
 #define CSV_SIMD_NEON 0x04
 #define CSV_SIMD_AVX512 0x08
 #define CSV_SIMD_SVE 0x10
 
 // Endianness detection
 #if defined(__BYTE_ORDER__)
     #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
         #define SONICSV_LITTLE_ENDIAN 1
         #define SONICSV_BIG_ENDIAN 0
     #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
         #define SONICSV_LITTLE_ENDIAN 0
         #define SONICSV_BIG_ENDIAN 1
     #endif
 #elif defined(__LITTLE_ENDIAN__) || defined(__ARMEL__) || defined(__THUMBEL__) || \
       defined(__AARCH64EL__) || defined(_MIPSEL) || defined(__MIPSEL) || defined(__MIPSEL__) || \
       defined(__x86_64__) || defined(__i386__) || defined(_X86_) || defined(__X86__)
     #define SONICSV_LITTLE_ENDIAN 1
     #define SONICSV_BIG_ENDIAN 0
 #else
     #define SONICSV_LITTLE_ENDIAN 0
     #define SONICSV_BIG_ENDIAN 1
 #endif
 
 // Byte swapping for big-endian support (used in NEON path)
 #if SONICSV_BIG_ENDIAN
 static sonicsv_always_inline uint64_t csv_bswap64(uint64_t x) {
     return ((x & 0xFF00000000000000ULL) >> 56) | ((x & 0x00FF000000000000ULL) >> 40) |
            ((x & 0x0000FF0000000000ULL) >> 24) | ((x & 0x000000FF00000000ULL) >> 8)  |
            ((x & 0x00000000FF000000ULL) << 8)  | ((x & 0x0000000000FF0000ULL) << 24) |
            ((x & 0x000000000000FF00ULL) << 40) | ((x & 0x00000000000000FFULL) << 56);
 }
 #endif
 
 // Architecture detection with proper alignment.
 //
 // AVX-512 compilation is gated:
 //   - GCC needs >= 4.9 for AVX-512 intrinsics
 //   - Clang needs >= 3.7
 //   - MSVC has supported them since at least VS 2017 (15.3, _MSC_VER >= 1911)
 // Users on older toolchains may also opt out by defining
 // SONICSV_DISABLE_AVX512 before including this header.
 #if defined(__x86_64__) || defined(_M_X64)
#if defined(__GNUC__) || defined(__clang__)
#define HAVE_SSE4_2
#define HAVE_AVX2
#  if !defined(SONICSV_DISABLE_AVX512) && \
      ((defined(__clang_major__) && (__clang_major__ > 3 || (__clang_major__ == 3 && __clang_minor__ >= 7))) || \
       (defined(__GNUC__) && !defined(__clang__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 9))))
#    define HAVE_AVX512
#  endif
#include <immintrin.h>
#include <nmmintrin.h>
#include <cpuid.h>
#elif defined(_MSC_VER)
// MSVC accepts many x86 intrinsics even when the translation unit is compiled
// without matching /arch flags. Runtime CPUID alone is not enough in that mode:
// CI and downstream consumers often build the single-header implementation with
// plain /O2, so keep the MSVC default scalar-correct. Projects that compile the
// implementation TU with suitable /arch flags can opt in to the SIMD paths.
#include <intrin.h>
#  if defined(SONICSV_MSVC_ENABLE_SIMD)
#    define HAVE_SSE4_2
#    define HAVE_AVX2
#    if !defined(SONICSV_DISABLE_AVX512) && _MSC_VER >= 1911
#      define HAVE_AVX512
#    endif
#    include <immintrin.h>
#  endif
 #endif
 #define SONICSV_SIMD_ALIGN 32
 #endif
 
 #if defined(__aarch64__) || defined(_M_ARM64)
 #define HAVE_NEON
 #include <arm_neon.h>
 #define SONICSV_SIMD_ALIGN 16
 #if defined(__ARM_FEATURE_SVE)
 #define HAVE_SVE
 #include <arm_sve.h>
 #endif
 #endif
 
 #ifndef SONICSV_SIMD_ALIGN
 #define SONICSV_SIMD_ALIGN 16
 #endif
 
/*
 * Error codes returned by all parsing functions.
 * Check return values and use csv_error_string() for human-readable messages.
 */
typedef enum {
  CSV_OK = 0,                     /* Success - no error */
  CSV_ERROR_INVALID_ARGS = -1,    /* NULL pointer or invalid parameter */
  CSV_ERROR_OUT_OF_MEMORY = -2,   /* Memory allocation failed */
  CSV_ERROR_PARSE_ERROR = -6,     /* Malformed CSV (strict mode only) */
  CSV_ERROR_FIELD_TOO_LARGE = -7, /* Field exceeds max_field_size */
  CSV_ERROR_ROW_TOO_LARGE = -8,   /* Row exceeds max_row_size */
  CSV_ERROR_IO_ERROR = -9         /* File I/O failure */
} csv_error_t;
 
/*
 * Parser configuration options.
 * Use csv_default_options() to get sensible defaults, then customize as needed.
 */
typedef struct {
  /* Basic CSV format settings */
  char delimiter;          /* Field separator character (default: ',') */
  char quote_char;         /* Quote character for escaping (default: '"') */
  bool double_quote;       /* Treat "" as escaped quote (default: true) */
  bool trim_whitespace;    /* Strip leading/trailing spaces (default: false) */
  bool ignore_empty_lines; /* Skip blank lines (default: true) */
  bool strict_mode;        /* Error on malformed CSV (default: false) */

  /* Size limits - prevents memory exhaustion on malformed input */
  size_t max_field_size;   /* Max bytes per field (default: 10MB) */
  size_t max_row_size;     /* Max bytes per row (default: 100MB) */
  size_t buffer_size;      /* I/O buffer size (default: 64KB) */
  size_t max_memory_kb;    /* Memory limit in KB, 0=unlimited (default: 0) */
  size_t current_memory;   /* Internal: current memory usage tracking */

  /* Performance tuning */
  bool disable_mmap;       /* Force stream I/O instead of mmap (default: false) */
  int num_threads;         /* Reserved for future parallel parsing */
  bool enable_parallel;    /* Reserved for future parallel parsing */
  bool enable_prefetch;    /* Use CPU prefetch hints (default: true) */
  size_t prefetch_distance;/* Prefetch lookahead in bytes */
  bool force_alignment;    /* Force SIMD-aligned allocations (default: true) */
} csv_parse_options_t;
 
/*
 * A single CSV field (cell).
 * Access via csv_get_field(row, index).
 *
 * IMPORTANT: 'data' is NOT null-terminated for unquoted fields!
 * Always use 'size' to determine the field length.
 * Example: printf("%.*s", (int)field->size, field->data);
 */
typedef struct sonicsv_aligned(8) {
  const char *data;  /* Pointer to field content */
  size_t size;       /* Length in bytes (use this, not strlen!) */
  bool quoted;       /* true if field was quoted in source CSV */
} csv_field_t;

/*
 * A parsed CSV row, passed to your row callback.
 * Contains an array of fields and metadata about the row.
 */
typedef struct sonicsv_aligned(8) {
  csv_field_t *fields;   /* Array of fields in this row */
  size_t num_fields;     /* Number of fields (columns) */
  uint64_t row_number;   /* 1-based row number in the file */
  size_t byte_offset;    /* Byte offset of this row in the input */
} csv_row_t;
 
 // Enhanced statistics with memory and SIMD metrics
 typedef struct sonicsv_aligned(64) {
   uint64_t total_bytes_processed;
   uint64_t total_rows_parsed;
   uint64_t total_fields_parsed;
   uint64_t parse_time_ns;
   double throughput_mbps;
   uint32_t simd_acceleration_used;
   uint32_t peak_memory_kb;
   uint32_t errors_encountered;
   // Performance counters
   struct {
     uint64_t simd_ops;
     uint64_t scalar_fallbacks;
     double avg_field_size;
     double avg_row_size;
     double memory_efficiency;
   } perf;
 } csv_stats_t;
 
/* Opaque parser handle - create with csv_parser_create() */
typedef struct csv_parser csv_parser_t;

/* Opaque string pool handle - for string deduplication */
typedef struct csv_string_pool csv_string_pool_t;

/*
 * Callback function types.
 * Set these with csv_parser_set_row_callback() and csv_parser_set_error_callback().
 */

/* Called once for each parsed row */
typedef void (*csv_row_callback_t)(const csv_row_t *row, void *user_data);

/* Called when a parse error occurs */
typedef void (*csv_error_callback_t)(csv_error_t error, const char *message,
                                     uint64_t row_number, void *user_data);

/* ============================================================================
 * CORE API - Parser lifecycle and parsing functions
 * ============================================================================ */

/* Create a new parser. Pass NULL for default options, or customize. */
csv_parser_t *csv_parser_create(const csv_parse_options_t *options);

/* Free all resources. Always call when done with a parser. */
void csv_parser_destroy(csv_parser_t *parser);

/* Reset parser state for reuse (avoids reallocation). */
csv_error_t csv_parser_reset(csv_parser_t *parser);

/* Set callback invoked for each parsed row. */
void csv_parser_set_row_callback(csv_parser_t *parser, csv_row_callback_t callback, void *user_data);

/* Set callback invoked on parse errors. */
void csv_parser_set_error_callback(csv_parser_t *parser, csv_error_callback_t callback, void *user_data);

/* Parse a raw buffer. Set is_final=true for the last chunk. */
csv_error_t csv_parse_buffer(csv_parser_t *parser, const char *buffer, size_t size, bool is_final);

/* Parse an entire file (uses mmap for best performance). */
csv_error_t csv_parse_file(csv_parser_t *parser, const char *filename);

/* Parse from a FILE* stream. */
csv_error_t csv_parse_stream(csv_parser_t *parser, FILE *stream);

/* Parse a null-terminated string. */
csv_error_t csv_parse_string(csv_parser_t *parser, const char *csv_string);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/* Get default options (call this, then modify as needed). */
csv_parse_options_t csv_default_options(void);

/* Convert error code to human-readable string. */
const char *csv_error_string(csv_error_t error);

/* Get detailed performance statistics. */
csv_stats_t csv_parser_get_stats(const csv_parser_t *parser);

/* Print performance statistics to stdout. */
void csv_print_stats(const csv_parser_t *parser);

/* Get detected SIMD features (bitmask of CSV_SIMD_* flags). */
uint32_t csv_get_simd_features(void);

/* Get field at index from a row (returns NULL if index out of bounds). */
const csv_field_t *csv_get_field(const csv_row_t *row, size_t index);

/* Get number of fields in a row. */
size_t csv_get_num_fields(const csv_row_t *row);

/* ============================================================================
 * STRING POOL (optional) - For deduplicating repeated field values
 * ============================================================================ */

/* Create a string pool for interning repeated strings. */
csv_string_pool_t *csv_string_pool_create(size_t initial_capacity);

/* Destroy a string pool and free all memory. */
void csv_string_pool_destroy(csv_string_pool_t *pool);

/* Intern a string (returns existing pointer if already interned). */
const char *csv_string_pool_intern(csv_string_pool_t *pool, const char *str, size_t length);

/* Clear all interned strings (for reuse without reallocation). */
void csv_string_pool_clear(csv_string_pool_t *pool);
 
 #ifdef __cplusplus
 }
 #endif
 
 #ifdef SONICSV_IMPLEMENTATION

/* ---------------------------------------------------------------------------
 * Atomics: prefer C11 <stdatomic.h>; fall back to a minimal Interlocked-based
 * shim for MSVC versions that don't ship it (or that are built without
 * /experimental:c11atomics or /std:c11). The shim covers exactly the subset
 * sonicsv uses: atomic_uint, atomic_size_t, _Atomic(uint64_t) and the
 * load / store / fetch_add / fetch_sub / compare_exchange_weak_explicit ops.
 * Assumes a 64-bit target (x64 or arm64) — all atomics are stored as 64-bit.
 *
 * Kept inside SONICSV_IMPLEMENTATION so the public header stays usable from
 * C++ TUs (which would otherwise see <stdatomic.h> conflict with <atomic>).
 * ------------------------------------------------------------------------- */
#if defined(__cplusplus)
// C++ mode: <stdatomic.h> conflicts with <atomic>. Map the C atomic spellings
// sonicsv uses onto the C++ std::atomic types so the implementation compiles
// from a C++ TU. Performance equivalent; semantics match.
#  include <atomic>
typedef std::atomic<unsigned int> atomic_uint;
typedef std::atomic<size_t> atomic_size_t;
#  define _Atomic(T) std::atomic<T>
#  define memory_order_relaxed std::memory_order_relaxed
#  define memory_order_consume std::memory_order_consume
#  define memory_order_acquire std::memory_order_acquire
#  define memory_order_release std::memory_order_release
#  define memory_order_acq_rel std::memory_order_acq_rel
#  define memory_order_seq_cst std::memory_order_seq_cst
#  define atomic_load_explicit(p, mo) ((p)->load(mo))
#  define atomic_store_explicit(p, v, mo) ((p)->store((v), (mo)))
#  define atomic_fetch_add_explicit(p, v, mo) ((p)->fetch_add((v), (mo)))
#  define atomic_fetch_sub_explicit(p, v, mo) ((p)->fetch_sub((v), (mo)))
#  define atomic_compare_exchange_weak_explicit(p, exp, des, smo, fmo) \
       ((p)->compare_exchange_weak(*(exp), (des), (smo), (fmo)))
#  ifndef ATOMIC_VAR_INIT
#    define ATOMIC_VAR_INIT(x) (x)
#  endif
#elif defined(_MSC_VER) && !defined(__clang__) && defined(SONICSV_HAVE_STDATOMIC)
#  include <stdatomic.h>
#elif defined(_MSC_VER) && !defined(__clang__)
#  include <intrin.h>

typedef volatile long long atomic_uint;
typedef volatile long long atomic_size_t;
#  define _Atomic(T) volatile long long

#  define memory_order_relaxed 0
#  define memory_order_consume 0
#  define memory_order_acquire 0
#  define memory_order_release 0
#  define memory_order_acq_rel 0
#  define memory_order_seq_cst 0

static __forceinline unsigned long long sonicsv__atomic_load(volatile long long *p) {
    return (unsigned long long)_InterlockedOr64(p, 0);
}
static __forceinline void sonicsv__atomic_store(volatile long long *p, unsigned long long v) {
    (void)_InterlockedExchange64(p, (long long)v);
}
static __forceinline unsigned long long sonicsv__atomic_addsub(volatile long long *p, long long d) {
    return (unsigned long long)_InterlockedExchangeAdd64(p, d);
}
static __forceinline int sonicsv__atomic_cas(volatile long long *p,
                                             unsigned long long *expected,
                                             unsigned long long desired) {
    long long prev = _InterlockedCompareExchange64(p, (long long)desired, (long long)*expected);
    if ((unsigned long long)prev == (unsigned long long)*expected) return 1;
    *expected = (unsigned long long)prev;
    return 0;
}

#  define atomic_load_explicit(p, mo) \
      ((void)(mo), sonicsv__atomic_load((volatile long long *)(p)))
#  define atomic_store_explicit(p, v, mo) \
      ((void)(mo), sonicsv__atomic_store((volatile long long *)(p), (unsigned long long)(v)))
#  define atomic_fetch_add_explicit(p, v, mo) \
      ((void)(mo), sonicsv__atomic_addsub((volatile long long *)(p), (long long)(v)))
#  define atomic_fetch_sub_explicit(p, v, mo) \
      ((void)(mo), sonicsv__atomic_addsub((volatile long long *)(p), -(long long)(v)))
#  define atomic_compare_exchange_weak_explicit(p, exp, des, smo, fmo) \
      ((void)(smo), (void)(fmo), \
       sonicsv__atomic_cas((volatile long long *)(p), \
                           (unsigned long long *)(exp), \
                           (unsigned long long)(des)))
#else
#  include <stdatomic.h>
#endif

// Platform-specific implementation includes
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#  define NOMINMAX
#  endif
#  include <windows.h>
#  include <malloc.h>   /* _aligned_malloc / _aligned_free */
#else
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <unistd.h>
#endif

/* Portable monotonic clock: fills a struct timespec on all platforms. */
static sonicsv_always_inline void csv_clock_now(struct timespec *ts) {
#ifdef _WIN32
    static LARGE_INTEGER s_freq;
    LARGE_INTEGER count;
    if (s_freq.QuadPart == 0) QueryPerformanceFrequency(&s_freq);
    QueryPerformanceCounter(&count);
    ts->tv_sec  = (time_t)(count.QuadPart / s_freq.QuadPart);
    ts->tv_nsec = (long)(((count.QuadPart % s_freq.QuadPart) * 1000000000LL) / s_freq.QuadPart);
#else
    clock_gettime(CLOCK_MONOTONIC, ts);
#endif
}

/* Low-level platform aligned allocator. Pair csv_sys_aligned_alloc with csv_sys_aligned_free.
 * We use posix_memalign on every POSIX target rather than C11 aligned_alloc:
 * the latter is only declared by glibc when __STDC_VERSION__ >= 201112L (or
 * _ISOC11_SOURCE is set), so a -std=c99 build trips an implicit-declaration
 * error. posix_memalign is available since POSIX 2001 and we already require
 * _POSIX_C_SOURCE >= 200809L, so it's universally visible here. */
static sonicsv_always_inline void *csv_sys_aligned_alloc(size_t bytes, size_t alignment) {
#if defined(_WIN32)
    return _aligned_malloc(bytes, alignment);
#else
    void *p = NULL;
    size_t a = alignment >= sizeof(void*) ? alignment : sizeof(void*);
    if (posix_memalign(&p, a, bytes) != 0) return NULL;
    return p;
#endif
}

static sonicsv_always_inline void csv_sys_aligned_free(void *p) {
#if defined(_WIN32)
    _aligned_free(p);
#else
    free(p);
#endif
}
 
 // Memory allocation tracking with recycling
 typedef struct csv_alloc_header {
     size_t size;
     size_t alignment;
     uint32_t magic;
     struct csv_alloc_header* next;
     struct csv_alloc_header* prev;
 } csv_alloc_header_t;
 

// Safe multiplication to prevent integer overflow
static sonicsv_always_inline bool csv_safe_mul(size_t a, size_t b, size_t *result) {
    if (b != 0 && a > SIZE_MAX / b) return false;
    *result = a * b;
    return true;
}

#define CSV_ALLOC_MAGIC 0xDEADBEEF
 static atomic_size_t g_total_allocated = 0;
 static atomic_size_t g_peak_allocated = 0;
 static atomic_size_t g_allocation_count = 0;
 
 // Thread-safe allocation tracking
 static sonicsv_always_inline void csv_track_allocation(csv_alloc_header_t* header) {
     size_t new_total = atomic_fetch_add_explicit(&g_total_allocated, header->size, memory_order_relaxed) + header->size;
     atomic_fetch_add_explicit(&g_allocation_count, 1, memory_order_relaxed);
 
     // Update peak if necessary (racy but approximate is fine)
     size_t current_peak = atomic_load_explicit(&g_peak_allocated, memory_order_relaxed);
     while (new_total > current_peak) {
         if (atomic_compare_exchange_weak_explicit(&g_peak_allocated, &current_peak, new_total,
                                                 memory_order_relaxed, memory_order_relaxed)) {
             break;
         }
     }
 }
 
 static sonicsv_always_inline void csv_untrack_allocation(csv_alloc_header_t* header) {
     if (header->magic == CSV_ALLOC_MAGIC) {
         atomic_fetch_sub_explicit(&g_total_allocated, header->size, memory_order_relaxed);
         atomic_fetch_sub_explicit(&g_allocation_count, 1, memory_order_relaxed);
     }
 }
 
 
 
// Try to get block from recycling pool
// NOTE: Pool recycling disabled due to size-class bug where blocks of different
// sizes within the same class could be reused, causing buffer overflows.
// See: https://github.com/anthropics/claude-code/issues/XXX
static sonicsv_always_inline void* csv_pool_alloc(size_t size, size_t alignment) {
    (void)size;
    (void)alignment;
    return NULL; // Disabled - always allocate fresh
}
 
// Return block to recycling pool
// NOTE: Pool recycling disabled - see csv_pool_alloc comment
static sonicsv_always_inline bool csv_pool_free(void* ptr, size_t size) {
    (void)ptr;
    (void)size;
    return false; // Disabled - always free to system
}
 
// Safe memory-aligned buffer allocation with tracking and recycling
// Uses a direct offset pointer for O(1) header lookup instead of magic number search
static sonicsv_always_inline void* csv_aligned_alloc(size_t size, size_t alignment) {
    if (size == 0) return NULL;
    if (alignment == 0) alignment = sizeof(void*);
    // Ensure alignment is power of 2 and at least pointer-sized
    if ((alignment & (alignment - 1)) != 0) return NULL;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);

    // Prevent integer overflow with safe arithmetic
    if (size > SIZE_MAX - sizeof(csv_alloc_header_t) - alignment - sizeof(void*)) return NULL;

    // Include space for offset pointer before user data
    size_t total_size = sizeof(csv_alloc_header_t) + alignment + sizeof(void*) + size;
    void* raw_ptr = csv_pool_alloc(total_size, alignment);

    if (!raw_ptr) {
        raw_ptr = csv_sys_aligned_alloc(total_size, alignment);
        if (!raw_ptr) return NULL;
    }

    // Initialize header at the start
    csv_alloc_header_t* header = (csv_alloc_header_t*)raw_ptr;
    header->size = size;
    header->alignment = alignment;
    header->magic = CSV_ALLOC_MAGIC;
    header->next = NULL;
    header->prev = NULL;

    csv_track_allocation(header);

    // Calculate aligned user pointer (leave room for offset pointer)
    char* after_header = (char*)raw_ptr + sizeof(csv_alloc_header_t) + sizeof(void*);
    size_t misalignment = (uintptr_t)after_header % alignment;
    char* user_ptr = after_header;
    if (misalignment) {
        user_ptr += alignment - misalignment;
    }

    // Store pointer to header directly before user data (for O(1) lookup on free)
    void** header_ptr_slot = (void**)(user_ptr - sizeof(void*));
    *header_ptr_slot = header;

    // NOTE: user portion is intentionally NOT zero-initialized. Internal callers
    // either memset what they need or track usage via explicit size/pos counters,
    // so zeroing the full allocation here is wasted work (esp. on growth realloc
    // of large buffers like field_data_pool). Use csv_aligned_calloc when zeroed
    // memory is required.
    return user_ptr;
}

// Zero-initialized variant for callers that require it (e.g., hash-bucket arrays
// where empty entries are detected via NULL pointers).
static sonicsv_always_inline void* csv_aligned_calloc(size_t size, size_t alignment) {
    void* p = csv_aligned_alloc(size, alignment);
    if (p) memset(p, 0, size);
    return p;
}

static sonicsv_always_inline void csv_aligned_free(void* ptr) {
    if (!ptr) return;

    // Direct O(1) header lookup via stored pointer
    void** header_ptr_slot = (void**)((char*)ptr - sizeof(void*));
    csv_alloc_header_t* header = (csv_alloc_header_t*)*header_ptr_slot;

    // Validate header with bounds check
    if (header && (uintptr_t)header < (uintptr_t)ptr && header->magic == CSV_ALLOC_MAGIC) {
        csv_untrack_allocation(header);
        size_t original_size = header->size;
        header->magic = 0; // Clear magic to detect double-free

        // Try to recycle the block
        if (!csv_pool_free(header, original_size)) {
            csv_sys_aligned_free(header);
        }
    }
    // Silently ignore invalid pointers for safety
}
 
 // Memory statistics
 static sonicsv_always_inline size_t csv_get_allocated_memory(void) {
     return atomic_load_explicit(&g_total_allocated, memory_order_relaxed);
 }

 static sonicsv_always_inline size_t csv_get_peak_memory(void) {
     return atomic_load_explicit(&g_peak_allocated, memory_order_relaxed);
 }
 
 // Optimized buffer sizes based on typical CSV patterns
 #define CSV_INITIAL_FIELD_CAPACITY 512
 #define CSV_INITIAL_BUFFER_CAPACITY 16384
 #define CSV_FIELD_DATA_POOL_INITIAL 32768
 #define CSV_GROWTH_FACTOR 1.5  // Less aggressive growth to reduce memory waste
 
 // SIMD feature detection - single atomic with initialized flag in high bit
#define CSV_SIMD_INITIALIZED_FLAG 0x80000000U
static atomic_uint g_simd_features_atomic = 0;
 
 // Parser state machine
 typedef enum {
   CSV_STATE_FIELD_START,
   CSV_STATE_IN_QUOTED_FIELD,
   CSV_STATE_QUOTE_IN_QUOTED_FIELD,
 } csv_parse_state_t;
 
 // Thread-local parser statistics for isolation
 typedef struct {
   uint64_t simd_ops;
   uint64_t scalar_fallbacks;
 } csv_thread_stats_t;

 static SONICSV_THREAD_LOCAL csv_thread_stats_t g_thread_stats = {0, 0};

 // Internal parser structure - isolated and thread-safe.
 // NOTE: alignment of the *pointed-to* SIMD buffers is handled at allocation
 // time by csv_aligned_alloc. The pointer slots themselves don't need any
 // alignment attribute, and a postfix __declspec(align(...)) is rejected by
 // MSVC anyway (it must be prefix). Keep these as plain pointer fields.
 struct csv_parser {
   csv_parse_options_t options;
   csv_row_callback_t row_callback;
   void *row_callback_data;
   csv_error_callback_t error_callback;
   void *error_callback_data;
   csv_parse_state_t state;
   char* unparsed_buffer;
   size_t unparsed_size;
   size_t unparsed_capacity;
   csv_field_t *fields;
   size_t fields_capacity;
   size_t num_fields;
   char *field_buffer;
   size_t field_buffer_capacity;
   size_t field_buffer_pos;
   // Optimized field data storage with better memory layout
   char *field_data_pool;
   size_t field_data_pool_size;
   size_t field_data_pool_capacity;
   csv_stats_t stats;
   struct timespec start_time;
   size_t peak_memory;
   uint64_t current_row_start_offset;
   // Incremental running totals (avoid per-field/per-row FP divisions in hot path).
   // Averages are derived in csv_parser_get_stats from these counters.
   uint64_t sum_field_size;
   uint64_t sum_row_size;
   size_t current_row_size; // total bytes of fields in row being assembled
   uint32_t simd_features;
   // Set when a chunk consumed a `\r` as a row terminator without seeing the
   // following byte. If the next chunk starts with `\n`, it's the second half
   // of a CRLF and must be skipped — without this flag the parser would emit
   // an empty row for the lone `\n`.
   bool pending_lf_skip;
   // Parser instance ID for debugging
   uint64_t instance_id;
 } sonicsv_aligned(64);
 
 // Thread-safe instance ID generation
 static _Atomic(uint64_t) g_next_parser_id = 1;
 
 // --- Enhanced Runtime SIMD Feature Detection ---
 #if defined(__x86_64__) || defined(_M_X64)
 #if defined(__GNUC__) || defined(__clang__)
 static sonicsv_cold uint64_t csv_xgetbv0(void) {
     uint32_t eax, edx;
     __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
     return ((uint64_t)edx << 32) | eax;
 }
 #endif

 static sonicsv_cold uint32_t csv_detect_x86_features(void) {
     uint32_t features = CSV_SIMD_NONE;

 #if defined(__GNUC__) || defined(__clang__)
     unsigned int eax, ebx, ecx, edx;

     // Check if CPUID is supported
     if (__get_cpuid_max(0, NULL) == 0) return features;

     bool os_avx = false;
     bool os_avx512 = false;

     // Check basic features (SSE4.2 and OS SIMD state support)
     if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
         if (ecx & bit_SSE4_2) {
             // Simple feature flag check - modern OSes handle SIMD context switching
             features |= CSV_SIMD_SSE4_2;
         }
         if ((ecx & bit_AVX) && (ecx & bit_OSXSAVE)) {
             uint64_t xcr0 = csv_xgetbv0();
             os_avx = (xcr0 & 0x6) == 0x6;
             os_avx512 = (xcr0 & 0xE6) == 0xE6;
         }
     }

     // Check extended features (AVX2, AVX-512)
     if (__get_cpuid_max(0, NULL) >= 7) {
         if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
             // Check AVX2 support
             if (os_avx && (ebx & bit_AVX2)) {
                 features |= CSV_SIMD_AVX2;
             }

             // AVX-512 byte compares require AVX-512F + AVX-512BW and OS ZMM state.
             if (os_avx512 && (ebx & bit_AVX512F) && (ebx & bit_AVX512BW)) {
                 features |= CSV_SIMD_AVX512;
             }
         }
     }
 #elif defined(_MSC_VER)
#  if !defined(SONICSV_MSVC_ENABLE_SIMD)
     return features;
#  else
     // MSVC equivalents: __cpuid / __cpuidex / _xgetbv. Bit positions match
     // the GCC <cpuid.h> bit_* constants by Intel SDM definitions, redefined
     // here so we don't depend on the header.
     int info[4];
     __cpuid(info, 0);
     int max_leaf = info[0];
     if (max_leaf < 1) return features;

     bool os_avx = false;
     bool os_avx512 = false;

     __cpuid(info, 1);
     // ECX bit 20 = SSE4.2, bit 27 = OSXSAVE, bit 28 = AVX
     if (info[2] & (1 << 20)) features |= CSV_SIMD_SSE4_2;
     if ((info[2] & (1 << 28)) && (info[2] & (1 << 27))) {
         unsigned long long xcr0 = _xgetbv(0);
         os_avx = (xcr0 & 0x6) == 0x6;
         os_avx512 = (xcr0 & 0xE6) == 0xE6;
     }

     if (max_leaf >= 7) {
         __cpuidex(info, 7, 0);
         // EBX bit 5 = AVX2, bit 16 = AVX-512F, bit 30 = AVX-512BW
         if (os_avx && (info[1] & (1 << 5))) features |= CSV_SIMD_AVX2;
         if (os_avx512 && (info[1] & (1 << 16)) && (info[1] & (1 << 30)))
             features |= CSV_SIMD_AVX512;
     }
#  endif
 #endif
     return features;
 }
 #endif
 
 #if defined(__aarch64__) || defined(_M_ARM64)
 static sonicsv_cold uint32_t csv_detect_arm_features(void) {
     uint32_t features = CSV_SIMD_NONE;
 
     // NEON is standard on ARMv8-A (AArch64)
     features |= CSV_SIMD_NEON;
 
 #ifdef HAVE_SVE
     // Check for SVE support via system calls if needed
     // For now, assume SVE is available if compiled with support
     features |= CSV_SIMD_SVE;
 #endif
 
     return features;
 }
 #endif
 
static sonicsv_cold uint32_t csv_simd_init(void) {
    uint32_t features = CSV_SIMD_NONE;

#if defined(__x86_64__) || defined(_M_X64)
    features = csv_detect_x86_features();
#elif defined(__aarch64__) || defined(_M_ARM64)
    features = csv_detect_arm_features();
#endif

    features |= CSV_SIMD_INITIALIZED_FLAG;
    atomic_store_explicit(&g_simd_features_atomic, features, memory_order_relaxed);
    return features;
}
 
// --- Search result struct ---
typedef struct { const char *pos; size_t offset; } csv_search_result_t;

// SWAR helper macros for processing 8 bytes at a time
#define SWAR_BROADCAST(c) (0x0101010101010101ULL * (uint8_t)(c))
#define SWAR_HAS_ZERO(x) (((x) - 0x0101010101010101ULL) & ~(x) & 0x8080808080808080ULL)

// Find position of first matching byte in 64-bit word
static sonicsv_force_inline int csv_swar_find_first(uint64_t mask) {
#if SONICSV_LITTLE_ENDIAN
    return sonicsv_ctz64(mask) >> 3;
#else
    return sonicsv_clz64(mask) >> 3;
#endif
}

// --- Optimized SIMD implementations ---
// SWAR-optimized scalar fallback processes 8 bytes at a time
static sonicsv_force_inline csv_search_result_t csv_scalar_find_char(const char *d, size_t s, char c1, char c2, char c3, char c4) {
    if (sonicsv_unlikely(s == 0)) return (csv_search_result_t){NULL, 0};

#if defined(_MSC_VER) && !defined(__clang__)
    for (size_t i = 0; i < s; i++) {
        char c = d[i];
        if (c == c1 || c == c2 || c == c3 || c == c4)
            return (csv_search_result_t){d + i, i};
    }
    return (csv_search_result_t){NULL, s};
#else
    // Handle unaligned prefix
    size_t i = 0;
    size_t align_offset = (uintptr_t)d & 7;
    if (align_offset != 0) {
        size_t prefix_len = 8 - align_offset;
        if (prefix_len > s) prefix_len = s;
        for (; i < prefix_len; i++) {
            char c = d[i];
            if (c == c1 || c == c2 || c == c3 || c == c4)
                return (csv_search_result_t){d + i, i};
        }
    }

    // SWAR main loop - process 8 bytes at a time
    if (s >= 8) {
        uint64_t bc1 = SWAR_BROADCAST(c1);
        uint64_t bc2 = SWAR_BROADCAST(c2);
        uint64_t bc3 = SWAR_BROADCAST(c3);
        uint64_t bc4 = SWAR_BROADCAST(c4);

        for (; i + 8 <= s; i += 8) {
            uint64_t chunk;
            memcpy(&chunk, d + i, 8);

            // XOR with broadcast values - matching bytes become 0
            uint64_t m1 = SWAR_HAS_ZERO(chunk ^ bc1);
            uint64_t m2 = SWAR_HAS_ZERO(chunk ^ bc2);
            uint64_t m3 = SWAR_HAS_ZERO(chunk ^ bc3);
            uint64_t m4 = SWAR_HAS_ZERO(chunk ^ bc4);
            uint64_t combined = m1 | m2 | m3 | m4;

            if (combined) {
                int pos = csv_swar_find_first(combined);
                return (csv_search_result_t){d + i + pos, i + pos};
            }
        }
    }

    // Handle remaining bytes
    for (; i < s; i++) {
        char c = d[i];
        if (c == c1 || c == c2 || c == c3 || c == c4)
            return (csv_search_result_t){d + i, i};
    }

    return (csv_search_result_t){NULL, s};
#endif
}

#ifdef HAVE_AVX512
SONICSV_TARGET("avx512f,avx512bw")
static sonicsv_hot const char* csv_avx512_find_single_char(const char *d, size_t s, char target) {
    if (sonicsv_unlikely(!d || s < 64)) return NULL;

    const __m512i needle = _mm512_set1_epi8(target);
    size_t i = 0;
    for (; i + 64 <= s; i += 64) {
        __mmask64 mask = _mm512_cmpeq_epi8_mask(_mm512_loadu_si512((const void*)(d + i)), needle);
        if (mask) return d + i + sonicsv_ctz64((uint64_t)mask);
    }

    return NULL;
}
#endif

#ifdef HAVE_SSE4_2
SONICSV_TARGET("sse4.2")
static sonicsv_hot const char* csv_sse42_find_single_char(const char *d, size_t s, char target) {
    if (sonicsv_unlikely(!d || s < 16)) return NULL;

    const __m128i needle = _mm_set1_epi8(target);
    size_t i = 0;
    for (; i + 16 <= s; i += 16) {
        __m128i chunk = _mm_loadu_si128((const __m128i*)(d + i));
        uint32_t mask = (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, needle));
        if (mask) return d + i + sonicsv_ctz32(mask);
    }

    return NULL;
}
#endif

#ifdef HAVE_AVX2
SONICSV_TARGET("avx2")
static sonicsv_hot const char* csv_avx2_find_single_char(const char *d, size_t s, char target) {
    if (sonicsv_unlikely(!d || s < 32)) return NULL;

    const __m256i needle = _mm256_set1_epi8(target);
    size_t i = 0;
    for (; i + 32 <= s; i += 32) {
        __m256i chunk = _mm256_loadu_si256((const __m256i*)(d + i));
        uint32_t mask = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, needle));
        if (mask) return d + i + sonicsv_ctz32(mask);
    }

    return NULL;
}
#endif

// Optimized single-character search. Uses NEON / AVX2 / SSE4.2 when available
// and SWAR for portable tails.
static sonicsv_always_inline const char* csv_find_single_char(const char *d, size_t s, char target) {
    if (sonicsv_unlikely(s == 0)) return NULL;

#if defined(_MSC_VER) && !defined(__clang__)
    for (size_t i = 0; i < s; i++) {
        if (d[i] == target) return d + i;
    }
    return NULL;
#else
    size_t i = 0;

#if defined(__x86_64__) || defined(_M_X64)
    uint32_t features = csv_get_simd_features();
#ifdef HAVE_AVX512
    if (sonicsv_likely(features & CSV_SIMD_AVX512)) {
        const char *found = csv_avx512_find_single_char(d, s, target);
        if (found) return found;
        i = s & ~(size_t)63;
    } else
#endif
#ifdef HAVE_AVX2
    if (sonicsv_likely(features & CSV_SIMD_AVX2)) {
        const char *found = csv_avx2_find_single_char(d, s, target);
        if (found) return found;
        i = s & ~(size_t)31;
    } else
#endif
#ifdef HAVE_SSE4_2
    if (sonicsv_likely(features & CSV_SIMD_SSE4_2)) {
        const char *found = csv_sse42_find_single_char(d, s, target);
        if (found) return found;
        i = s & ~(size_t)15;
    }
#endif
#endif

#if defined(HAVE_NEON)
    if (s >= 16) {
        uint8x16_t v = vdupq_n_u8((uint8_t)target);
        for (; i + 16 <= s; i += 16) {
            uint8x16_t chunk = vld1q_u8((const uint8_t*)(d + i));
            uint8x16_t cmp = vceqq_u8(chunk, v);
            uint64_t lo = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 0);
            uint64_t hi = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 1);
#if SONICSV_BIG_ENDIAN
            lo = csv_bswap64(lo); hi = csv_bswap64(hi);
#endif
            if (lo) return d + i + (sonicsv_ctz64(lo) >> 3);
            if (hi) return d + i + 8 + (sonicsv_ctz64(hi) >> 3);
        }
    }
#endif

    // SWAR main loop for single char - process 8 bytes at a time
    if (s - i >= 8) {
        uint64_t broadcast = SWAR_BROADCAST(target);

        for (; i + 8 <= s; i += 8) {
            uint64_t chunk;
            memcpy(&chunk, d + i, 8);

            uint64_t match = SWAR_HAS_ZERO(chunk ^ broadcast);
            if (match) {
                int pos = csv_swar_find_first(match);
                return d + i + pos;
            }
        }
    }

    // Handle remaining bytes
    for (; i < s; i++) {
        if (d[i] == target) return d + i;
    }

    return NULL;
#endif
}

#ifdef HAVE_AVX512
 SONICSV_TARGET("avx512f,avx512bw")
 static sonicsv_hot csv_search_result_t csv_avx512_find_char(const char *d, size_t s, char c1, char c2, char c3, char c4) {
   if (sonicsv_unlikely(s < 64)) return csv_scalar_find_char(d, s, c1, c2, c3, c4);
   if (sonicsv_unlikely(!d || s == 0)) return (csv_search_result_t){NULL, s};

   __m512i v_c1 = _mm512_set1_epi8(c1), v_c2 = _mm512_set1_epi8(c2),
           v_c3 = _mm512_set1_epi8(c3), v_c4 = _mm512_set1_epi8(c4);
   size_t i = 0;

   for (; i + 64 <= s; i += 64) {
     __m512i chunk = _mm512_loadu_si512((const void*)(d + i));
     __mmask64 mask = _mm512_cmpeq_epi8_mask(chunk, v_c1) |
                      _mm512_cmpeq_epi8_mask(chunk, v_c2) |
                      _mm512_cmpeq_epi8_mask(chunk, v_c3) |
                      _mm512_cmpeq_epi8_mask(chunk, v_c4);
     if (mask) {
       size_t bit_pos = sonicsv_ctz64((uint64_t)mask);
       return (csv_search_result_t){d + i + bit_pos, i + bit_pos};
     }
   }

   if (i < s) {
     csv_search_result_t tail = csv_scalar_find_char(d + i, s - i, c1, c2, c3, c4);
     if (tail.pos) tail.offset += i;
     else tail.offset = s;
     return tail;
   }

   return (csv_search_result_t){NULL, s};
 }
#endif

#ifdef HAVE_SSE4_2
 SONICSV_TARGET("sse4.2")
 static sonicsv_hot csv_search_result_t csv_sse42_find_char(const char *d, size_t s, char c1, char c2, char c3, char c4) {
   if (sonicsv_unlikely(s < 16)) return csv_scalar_find_char(d, s, c1, c2, c3, c4);
 
   // Bounds check to prevent buffer overrun
   if (sonicsv_unlikely(!d || s == 0)) return (csv_search_result_t){NULL, s};
 
   __m128i v_c1 = _mm_set1_epi8(c1), v_c2 = _mm_set1_epi8(c2),
           v_c3 = _mm_set1_epi8(c3), v_c4 = _mm_set1_epi8(c4);
   size_t i = 0;
 
   // Safe loop with explicit bounds checking
   for (; i + 16 <= s && i <= s - 16; i += 16) {
     __m128i chunk = _mm_loadu_si128((__m128i const*)(d + i));
     __m128i cmp = _mm_or_si128(_mm_or_si128(
       _mm_or_si128(_mm_cmpeq_epi8(chunk, v_c1), _mm_cmpeq_epi8(chunk, v_c2)),
       _mm_cmpeq_epi8(chunk, v_c3)), _mm_cmpeq_epi8(chunk, v_c4));
     uint32_t mask = _mm_movemask_epi8(cmp);
     if (mask != 0) {
       size_t bit_pos = sonicsv_ctz32(mask);
       if (i + bit_pos < s) return (csv_search_result_t){d + i + bit_pos, i + bit_pos};
     }
   }
 
   // Safe tail processing
   if (i < s) {
     csv_search_result_t tail = csv_scalar_find_char(d + i, s - i, c1, c2, c3, c4);
     if (tail.pos) tail.offset += i;
     else tail.offset = s;
     return tail;
   }
 
   return (csv_search_result_t){NULL, s};
 }
 #endif
 
 #ifdef HAVE_AVX2
 SONICSV_TARGET("avx2")
 static sonicsv_hot csv_search_result_t csv_avx2_find_char(const char *d, size_t s, char c1, char c2, char c3, char c4) {
   if (sonicsv_unlikely(s < 32)) return csv_scalar_find_char(d, s, c1, c2, c3, c4);
 
   // Bounds check to prevent buffer overrun
   if (sonicsv_unlikely(!d || s == 0)) return (csv_search_result_t){NULL, s};
 
   __m256i v_c1 = _mm256_set1_epi8(c1), v_c2 = _mm256_set1_epi8(c2),
           v_c3 = _mm256_set1_epi8(c3), v_c4 = _mm256_set1_epi8(c4);
   size_t i = 0;
 
   // Safe loop with explicit bounds checking
   for (; i + 32 <= s && i <= s - 32; i += 32) {
     // Safe prefetch - only if we have enough remaining data
     if (i + 64 <= s) sonicsv_prefetch_read(d, i + 64);
     __m256i chunk = _mm256_loadu_si256((__m256i const*)(d + i));
     uint32_t mask = _mm256_movemask_epi8(_mm256_or_si256(_mm256_or_si256(
       _mm256_or_si256(_mm256_cmpeq_epi8(chunk, v_c1), _mm256_cmpeq_epi8(chunk, v_c2)),
       _mm256_cmpeq_epi8(chunk, v_c3)), _mm256_cmpeq_epi8(chunk, v_c4)));
     if (mask != 0) {
       size_t bit_pos = sonicsv_ctz32(mask);
       if (i + bit_pos < s) return (csv_search_result_t){d + i + bit_pos, i + bit_pos};
     }
   }
 
   // Safe tail processing
   if (i < s) {
     csv_search_result_t tail = csv_scalar_find_char(d + i, s - i, c1, c2, c3, c4);
     if (tail.pos) tail.offset += i;
     else tail.offset = s;
     return tail;
   }
 
   return (csv_search_result_t){NULL, s};
 }
 #endif
 
 #ifdef HAVE_NEON
 static sonicsv_hot csv_search_result_t csv_neon_find_char(const char *d, size_t s, char c1, char c2, char c3, char c4) {
   if (sonicsv_unlikely(s < 16)) return csv_scalar_find_char(d, s, c1, c2, c3, c4);
 
   // Bounds check to prevent buffer overrun
   if (sonicsv_unlikely(!d || s == 0)) return (csv_search_result_t){NULL, s};
 
   uint8x16_t v_c1 = vdupq_n_u8((uint8_t)c1), v_c2 = vdupq_n_u8((uint8_t)c2),
              v_c3 = vdupq_n_u8((uint8_t)c3), v_c4 = vdupq_n_u8((uint8_t)c4);
   size_t i = 0;
 
   // Safe loop with explicit bounds checking
   for (; i + 16 <= s && i <= s - 16; i += 16) {
     // Safe prefetch - only if we have enough remaining data
     if (i + 64 <= s) __builtin_prefetch(d + i + 64, 0, 3);
     uint8x16_t chunk = vld1q_u8((uint8_t const*)(d + i));
     uint8x16_t cmp = vorrq_u8(vorrq_u8(
       vorrq_u8(vceqq_u8(chunk, v_c1), vceqq_u8(chunk, v_c2)),
       vceqq_u8(chunk, v_c3)), vceqq_u8(chunk, v_c4));
 
     // Extract mask from comparison result - handle endianness
     uint64_t mask_lo = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 0);
     uint64_t mask_hi = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 1);
 
 #if SONICSV_BIG_ENDIAN
     mask_lo = csv_bswap64(mask_lo);
     mask_hi = csv_bswap64(mask_hi);
 #endif
 
     if (mask_lo != 0) {
       int pos = sonicsv_ctz64(mask_lo) / 8;
       if (i + pos < s) return (csv_search_result_t){d + i + pos, i + pos};
     }
     if (mask_hi != 0) {
       int pos = 8 + sonicsv_ctz64(mask_hi) / 8;
       if (i + pos < s) return (csv_search_result_t){d + i + pos, i + pos};
     }
   }
 
   // Safe tail processing
   if (i < s) {
     csv_search_result_t tail = csv_scalar_find_char(d + i, s - i, c1, c2, c3, c4);
     if (tail.pos) tail.offset += i;
     else tail.offset = s;
     return tail;
   }
 
   return (csv_search_result_t){NULL, s};
 }
 #endif

// Parser-isolated SIMD interface. The per-iteration prefetch inside each SIMD
// loop already covers ahead-of-stream lines; an upfront 32-line prefetch burst
// here is redundant and just pollutes the cache for short scans.
static sonicsv_always_inline csv_search_result_t csv_find_special_char_with_parser(csv_parser_t *parser, const char *d, size_t s, char del, char quo, char nl, char cr) {
    uint32_t features = parser ? parser->simd_features : csv_get_simd_features();
 
     // Dispatch to best available implementation
 #if defined(__x86_64__) || defined(_M_X64)
     #ifdef HAVE_AVX512
     if (sonicsv_likely(features & CSV_SIMD_AVX512)) {
         g_thread_stats.simd_ops++;
         return csv_avx512_find_char(d, s, del, quo, nl, cr);
     }
     #endif
     #ifdef HAVE_AVX2
     if (sonicsv_likely(features & CSV_SIMD_AVX2)) {
         g_thread_stats.simd_ops++;
         return csv_avx2_find_char(d, s, del, quo, nl, cr);
     }
     #endif
     #ifdef HAVE_SSE4_2
     if (sonicsv_likely(features & CSV_SIMD_SSE4_2)) {
         g_thread_stats.simd_ops++;
         return csv_sse42_find_char(d, s, del, quo, nl, cr);
     }
     #endif
 #elif defined(__aarch64__) || defined(_M_ARM64)
     #ifdef HAVE_NEON
     if (sonicsv_likely(features & CSV_SIMD_NEON)) {
         g_thread_stats.simd_ops++;
         return csv_neon_find_char(d, s, del, quo, nl, cr);
     }
     #endif
 #endif
 
     // Fallback to scalar implementation
     g_thread_stats.scalar_fallbacks++;
     return csv_scalar_find_char(d, s, del, quo, nl, cr);
 }
 
 // Backward compatibility wrapper (no parser context)
static sonicsv_always_inline csv_search_result_t csv_find_special_char(const char *d, size_t s, char del, char quo, char nl, char cr) {
    uint32_t features = csv_get_simd_features();

#if defined(__x86_64__) || defined(_M_X64)
    #ifdef HAVE_AVX512
    if (sonicsv_likely(features & CSV_SIMD_AVX512))
        return csv_avx512_find_char(d, s, del, quo, nl, cr);
    #endif
    #ifdef HAVE_AVX2
    if (sonicsv_likely(features & CSV_SIMD_AVX2))
        return csv_avx2_find_char(d, s, del, quo, nl, cr);
    #endif
    #ifdef HAVE_SSE4_2
    if (sonicsv_likely(features & CSV_SIMD_SSE4_2))
        return csv_sse42_find_char(d, s, del, quo, nl, cr);
    #endif
#elif defined(__aarch64__) || defined(_M_ARM64)
    #ifdef HAVE_NEON
    if (sonicsv_likely(features & CSV_SIMD_NEON))
        return csv_neon_find_char(d, s, del, quo, nl, cr);
    #endif
#endif

    return csv_scalar_find_char(d, s, del, quo, nl, cr);
}
 
// --- Enhanced helper functions ---
// Enhanced memory allocation with overflow protection and growth strategy
static sonicsv_force_inline csv_error_t ensure_capacity_safe(void **p, size_t *cap, size_t req, size_t el_sz, csv_parser_t *parser) {
    if (sonicsv_likely(*cap >= req)) return CSV_OK;

    // Validate inputs
    if (el_sz == 0) return CSV_ERROR_INVALID_ARGS;

    // Check for integer overflow using safe multiplication
    size_t req_size;
    if (!csv_safe_mul(req, el_sz, &req_size)) return CSV_ERROR_OUT_OF_MEMORY;

    // Calculate new capacity with growth factor
    size_t new_cap = *cap ? (size_t)(*cap * CSV_GROWTH_FACTOR) + 1 : 64;
    if (new_cap < req) new_cap = req;

    // Align to cache line boundaries for better performance
    new_cap = (new_cap + SONICSV_CACHE_LINE_SIZE - 1) & ~((size_t)(SONICSV_CACHE_LINE_SIZE - 1));

    // Safe multiplication for new size
    size_t new_size;
    if (!csv_safe_mul(new_cap, el_sz, &new_size)) return CSV_ERROR_OUT_OF_MEMORY;

    // Prevent excessive memory usage
    if (parser && parser->options.max_memory_kb > 0) {
        size_t current_memory = csv_get_allocated_memory();
        size_t old_size = *cap * el_sz;
        size_t max_allowed = parser->options.max_memory_kb * 1024;
        if (current_memory > old_size && (current_memory - old_size) + new_size > max_allowed) {
            return CSV_ERROR_OUT_OF_MEMORY;
        }
    }

    void *new_p = csv_aligned_alloc(new_size, SONICSV_SIMD_ALIGN);
    if (sonicsv_unlikely(!new_p)) return CSV_ERROR_OUT_OF_MEMORY;

    if (*p) {
        size_t copy_size = *cap * el_sz;
        memcpy(new_p, *p, copy_size);
        csv_aligned_free(*p);
     }
 
     *p = new_p;
     *cap = new_cap;
 
     // Update parser memory tracking
     if (parser) {
         parser->peak_memory = csv_get_peak_memory();
     }
 
     return CSV_OK;
 }
 
 static sonicsv_always_inline csv_error_t ensure_capacity(void **p, size_t *cap, size_t req, size_t el_sz, csv_parser_t *parser) {
     return ensure_capacity_safe(p, cap, req, el_sz, parser);
 }

// ----------------------------------------------------------------------------
// Field-pointer stabilization for chunked-feed correctness.
//
// The slow path emits unquoted fields zero-copy: the field's `data` pointer
// references the source buffer (the user's chunk or the parser-owned
// `unparsed_buffer`). When parsing in chunks (is_final=false), the parser
// retains the chunk's data inside `unparsed_buffer` so a partial row can
// resume on the next call. Two operations would invalidate previously-
// emitted field pointers if left unhandled:
//
//   1. ensure_capacity reallocating `unparsed_buffer` to a larger backing
//      allocation — the buffer moves and old pointers point at freed memory.
//   2. memmove compacting `unparsed_buffer` at end of a partial-save —
//      the bytes a field pointer references get overwritten by the shift.
//
// `csv_relocate_field_pointers` handles case 1: after a known relocation,
// it adjusts every field pointer that referenced the old buffer to the
// equivalent offset in the new buffer.
//
// `csv_stabilize_pending_fields` handles case 2: it copies the data of any
// pending field whose `data` lies in the about-to-be-mutated buffer into
// `field_data_pool` (which won't be reused or compacted during this parse),
// then re-points the field at the pool. This must happen BEFORE the memmove.

static sonicsv_force_inline void csv_relocate_field_pointers(
    csv_parser_t *p, const char *old_buf, size_t old_cap, const char *new_buf) {
    if (old_buf == new_buf || p->num_fields == 0 || !old_buf) return;
    const char *old_end = old_buf + old_cap;
    for (size_t i = 0; i < p->num_fields; i++) {
        csv_field_t *f = &p->fields[i];
        if (f->data >= old_buf && f->data < old_end) {
            f->data = new_buf + (f->data - old_buf);
        }
    }
}

// ensure_capacity wrapper that also relocates p->fields[] pointers if the
// buffer was moved by a reallocation. Use whenever the resized buffer may
// be referenced by p->fields[].data.
static sonicsv_force_inline csv_error_t ensure_capacity_relocate_fields(
    char **buf, size_t *cap, size_t req, csv_parser_t *p) {
    if (*cap >= req) return CSV_OK;
    char *old_buf = *buf;
    size_t old_cap = *cap;
    csv_error_t err = ensure_capacity((void**)buf, cap, req, 1, p);
    if (sonicsv_unlikely(err != CSV_OK)) return err;
    csv_relocate_field_pointers(p, old_buf, old_cap, *buf);
    return err;
}

// Copy any p->fields[] entries whose data lies within [buf_start, buf_end)
// into field_data_pool, re-pointing the field at the pool. Used before an
// in-place memmove of `buf` (whose contents are about to be overwritten).
//
// The pool is grown once upfront so individual copies don't trigger further
// relocations; any prior pool entries are also relocated by the single grow.
static sonicsv_force_inline csv_error_t csv_stabilize_pending_fields(
    csv_parser_t *p, const char *buf_start, const char *buf_end) {
    if (p->num_fields == 0) return CSV_OK;

    size_t need = 0;
    for (size_t i = 0; i < p->num_fields; i++) {
        const csv_field_t *f = &p->fields[i];
        if (f->data >= buf_start && f->data < buf_end) {
            need += f->size + 1;
        }
    }
    if (need == 0) return CSV_OK;

    size_t target = p->field_data_pool_size + need;
    if (target > p->field_data_pool_capacity) {
        size_t reserve = p->field_data_pool_capacity == 0 && target < CSV_FIELD_DATA_POOL_INITIAL
                         ? CSV_FIELD_DATA_POOL_INITIAL : target;
        csv_error_t err = ensure_capacity_relocate_fields(
            &p->field_data_pool, &p->field_data_pool_capacity, reserve, p);
        if (sonicsv_unlikely(err != CSV_OK)) return err;
    }

    for (size_t i = 0; i < p->num_fields; i++) {
        csv_field_t *f = &p->fields[i];
        if (f->data >= buf_start && f->data < buf_end) {
            char *dst = p->field_data_pool + p->field_data_pool_size;
            memcpy(dst, f->data, f->size);
            dst[f->size] = '\0';
            p->field_data_pool_size += f->size + 1;
            f->data = dst;
        }
    }
    return CSV_OK;
}

 static sonicsv_always_inline csv_error_t report_error(csv_parser_t *p, csv_error_t e, const char *m) {
   p->stats.errors_encountered++;
   if (p->error_callback) p->error_callback(e, m, p->stats.total_rows_parsed + 1, p->error_callback_data);
   return e;
 }
 
 // Optimized field processing with branch prediction hints
 static sonicsv_force_inline csv_error_t add_field(csv_parser_t *p, const char *d, size_t s, bool q) {
   if (sonicsv_unlikely(s > p->options.max_field_size))
     return report_error(p, CSV_ERROR_FIELD_TOO_LARGE, "Field size exceeds max_field_size");

   // Pre-allocate fields array with better prediction
   if (sonicsv_unlikely(p->num_fields >= p->fields_capacity)) {
     if (sonicsv_unlikely(ensure_capacity((void**)&p->fields, &p->fields_capacity,
                                          p->num_fields + 1, sizeof(csv_field_t), p) != CSV_OK))
       return CSV_ERROR_OUT_OF_MEMORY;
   }

   const char *field_data = d;

   if (sonicsv_unlikely(q)) {
     // Quoted field processing - copy into pool. Used when source is unstable
     // (e.g., field_buffer that will be reused for the next quoted field).
     // For stable sources (input buffer), prefer add_field_ref to skip this copy.
     //
     // Growing the pool may relocate it; previously-emitted quoted fields'
     // `data` references would go stale. ensure_capacity_relocate_fields
     // updates them along with the move.
     size_t required_size = p->field_data_pool_size + s + 1;
     if (sonicsv_unlikely(required_size > p->field_data_pool_capacity)) {
       size_t reserve_size = p->field_data_pool_capacity == 0 && required_size < CSV_FIELD_DATA_POOL_INITIAL
                             ? CSV_FIELD_DATA_POOL_INITIAL : required_size;
       if (sonicsv_unlikely(ensure_capacity_relocate_fields(&p->field_data_pool,
                                                            &p->field_data_pool_capacity,
                                                            reserve_size, p) != CSV_OK))
         return CSV_ERROR_OUT_OF_MEMORY;
     }

     field_data = p->field_data_pool + p->field_data_pool_size;
     memcpy((char*)field_data, d, s);
     ((char*)field_data)[s] = '\0';
     p->field_data_pool_size = required_size;
   } else if (sonicsv_unlikely(p->options.trim_whitespace && s > 0)) {
     // Unquoted field with trimming
     const char *start = d, *end = d + s;
     while (start < end && (*start == ' ' || *start == '\t')) start++;
     while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
     field_data = start;
     s = end - start;
   }

   // Track row size incrementally; max_row_size is enforced once per row in
   // finish_row (a per-field branch here was a measurable hot-path tax on
   // wide CSVs and matches the original semantics).
   p->current_row_size += s;

   // Store field with likely branch prediction
   csv_field_t *field = &p->fields[p->num_fields++];
   field->data = field_data;
   field->size = s;
   field->quoted = q;

   // Cheap integer-only stat update; averages computed lazily in get_stats.
   p->stats.total_fields_parsed++;
   p->sum_field_size += s;

   return CSV_OK;
 }

// Zero-copy variant: caller guarantees `d` remains valid for the duration of
// the row callback (true for pointers into the current input buffer or into
// the parser-owned unparsed_buffer that won't be reallocated mid-call). Used
// by the IN_QUOTED_FIELD fast path to skip both the field_buffer round-trip
// and the field_data_pool copy when no `""` escape was encountered.
static sonicsv_force_inline csv_error_t add_field_ref(csv_parser_t *p, const char *d, size_t s, bool q) {
  if (sonicsv_unlikely(s > p->options.max_field_size))
    return report_error(p, CSV_ERROR_FIELD_TOO_LARGE, "Field size exceeds max_field_size");

  if (sonicsv_unlikely(p->num_fields >= p->fields_capacity)) {
    if (sonicsv_unlikely(ensure_capacity((void**)&p->fields, &p->fields_capacity,
                                         p->num_fields + 1, sizeof(csv_field_t), p) != CSV_OK))
      return CSV_ERROR_OUT_OF_MEMORY;
  }

  const char *field_data = d;
  if (sonicsv_unlikely(!q && p->options.trim_whitespace && s > 0)) {
    const char *start = d, *end = d + s;
    while (start < end && (*start == ' ' || *start == '\t')) start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
    field_data = start;
    s = end - start;
  }

  p->current_row_size += s;

  csv_field_t *field = &p->fields[p->num_fields++];
  field->data = field_data;
  field->size = s;
  field->quoted = q;

  p->stats.total_fields_parsed++;
  p->sum_field_size += s;
  return CSV_OK;
}
 
 static sonicsv_force_inline csv_error_t finish_row(csv_parser_t *p) {
   // A "blank line" produces num_fields==1 with a single zero-byte unquoted
   // field (the FIELD_START path adds an empty field before calling
   // finish_row on a bare \n / \r). Treat that as the empty-line case so
   // ignore_empty_lines actually has an effect — the prior `num_fields==0`
   // guard never fires in practice.
   if (p->options.ignore_empty_lines &&
       (p->num_fields == 0 ||
        (p->num_fields == 1 && p->fields[0].size == 0 && !p->fields[0].quoted))) {
     p->num_fields = 0;
     p->current_row_size = 0;
     return CSV_OK;
   }

   if (sonicsv_unlikely(p->current_row_size > p->options.max_row_size))
     return report_error(p, CSV_ERROR_ROW_TOO_LARGE, "Row size exceeds max_row_size");

   p->stats.total_rows_parsed++;
   p->sum_row_size += p->current_row_size;

   if (p->row_callback) {
     csv_row_t row = {p->fields, p->num_fields, p->stats.total_rows_parsed, p->current_row_start_offset};
     p->row_callback(&row, p->row_callback_data);
   }

   p->num_fields = 0;
   p->current_row_size = 0;
   return CSV_OK;
 }
 
 static sonicsv_force_inline csv_error_t append_to_field_buffer(csv_parser_t *p, const char *s, size_t l) {
   if (sonicsv_unlikely(p->field_buffer_pos + l > p->options.max_field_size))
     return CSV_ERROR_FIELD_TOO_LARGE;
   size_t required_size = p->field_buffer_pos + l;
   size_t reserve_size = p->field_buffer_capacity == 0 && required_size < CSV_INITIAL_BUFFER_CAPACITY
                         ? CSV_INITIAL_BUFFER_CAPACITY : required_size;
   if (sonicsv_unlikely(ensure_capacity((void**)&p->field_buffer, &p->field_buffer_capacity,
                                        reserve_size, 1, p) != CSV_OK))
     return CSV_ERROR_OUT_OF_MEMORY;
 
   memcpy(p->field_buffer + p->field_buffer_pos, s, l);
   p->field_buffer_pos += l;
   return CSV_OK;
 }
 
// UTF-8 BOM detection constant
static const unsigned char CSV_UTF8_BOM[3] = {0xEF, 0xBB, 0xBF};

#ifdef HAVE_SSE4_2
SONICSV_TARGET("sse4.2")
static sonicsv_hot const char* csv_sse42_find_delim_or_newline(const char *d, size_t s, char delim) {
    const __m128i v_delim = _mm_set1_epi8(delim);
    const __m128i v_nl = _mm_set1_epi8('\n');
    const __m128i v_cr = _mm_set1_epi8('\r');

    for (size_t i = 0; i + 16 <= s; i += 16) {
        __m128i chunk = _mm_loadu_si128((const __m128i*)(d + i));
        __m128i cmp = _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(chunk, v_delim),
                                                _mm_cmpeq_epi8(chunk, v_nl)),
                                   _mm_cmpeq_epi8(chunk, v_cr));
        uint32_t mask = (uint32_t)_mm_movemask_epi8(cmp);
        if (mask) return d + i + sonicsv_ctz32(mask);
    }

    return NULL;
}
#endif

#ifdef HAVE_AVX512
SONICSV_TARGET("avx512f,avx512bw")
static sonicsv_hot const char* csv_avx512_find_delim_or_newline(const char *d, size_t s, char delim) {
    const __m512i v_delim = _mm512_set1_epi8(delim);
    const __m512i v_nl = _mm512_set1_epi8('\n');
    const __m512i v_cr = _mm512_set1_epi8('\r');

    for (size_t i = 0; i + 64 <= s; i += 64) {
        __m512i chunk = _mm512_loadu_si512((const void*)(d + i));
        __mmask64 mask = _mm512_cmpeq_epi8_mask(chunk, v_delim) |
                         _mm512_cmpeq_epi8_mask(chunk, v_nl) |
                         _mm512_cmpeq_epi8_mask(chunk, v_cr);
        if (mask) return d + i + sonicsv_ctz64((uint64_t)mask);
    }

    return NULL;
}
#endif

#ifdef HAVE_AVX2
SONICSV_TARGET("avx2")
static sonicsv_hot const char* csv_avx2_find_delim_or_newline(const char *d, size_t s, char delim) {
    const __m256i v_delim = _mm256_set1_epi8(delim);
    const __m256i v_nl = _mm256_set1_epi8('\n');
    const __m256i v_cr = _mm256_set1_epi8('\r');

    for (size_t i = 0; i + 32 <= s; i += 32) {
        __m256i chunk = _mm256_loadu_si256((const __m256i*)(d + i));
        __m256i cmp = _mm256_or_si256(_mm256_or_si256(_mm256_cmpeq_epi8(chunk, v_delim),
                                                      _mm256_cmpeq_epi8(chunk, v_nl)),
                                      _mm256_cmpeq_epi8(chunk, v_cr));
        uint32_t mask = (uint32_t)_mm256_movemask_epi8(cmp);
        if (mask) return d + i + sonicsv_ctz32(mask);
    }

    return NULL;
}
#endif

#ifdef HAVE_NEON
static sonicsv_hot const char* csv_neon_find_delim_or_newline(const char *d, size_t s, char delim) {
    if (sonicsv_unlikely(s < 16)) return NULL;
    const uint8x16_t v_delim = vdupq_n_u8((uint8_t)delim);
    const uint8x16_t v_nl = vdupq_n_u8('\n');
    const uint8x16_t v_cr = vdupq_n_u8('\r');

    for (size_t i = 0; i + 16 <= s; i += 16) {
        uint8x16_t chunk = vld1q_u8((const uint8_t*)(d + i));
        uint8x16_t cmp = vorrq_u8(vorrq_u8(vceqq_u8(chunk, v_delim),
                                           vceqq_u8(chunk, v_nl)),
                                  vceqq_u8(chunk, v_cr));
        uint64_t lo = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 0);
        uint64_t hi = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 1);
#if SONICSV_BIG_ENDIAN
        lo = csv_bswap64(lo); hi = csv_bswap64(hi);
#endif
        if (lo) return d + i + (sonicsv_ctz64(lo) >> 3);
        if (hi) return d + i + 8 + (sonicsv_ctz64(hi) >> 3);
    }
    return NULL;
}
#endif

// 3-char SWAR search for the simple-fast path (delimiter, '\n', '\r').
// Caller is responsible for ensuring the buffer is quote-free (validated
// upfront with one SIMD pass) — this keeps the per-iteration scan as cheap
// as possible since quoted-CSV correctness is handled by the slow path.
static sonicsv_always_inline const char* csv_find_delim_or_newline(const char *d, size_t s, char delim, uint32_t features) {
    if (s == 0) return NULL;

#if defined(_MSC_VER) && !defined(__clang__)
    (void)features;
    for (size_t i = 0; i < s; i++) {
        char c = d[i];
        if (c == delim || c == '\n' || c == '\r') return d + i;
    }
    return NULL;
#else
    size_t i = 0;
    uint64_t bc_delim = SWAR_BROADCAST(delim);
    uint64_t bc_nl = SWAR_BROADCAST('\n');
    uint64_t bc_cr = SWAR_BROADCAST('\r');

#ifndef __x86_64__
    (void)features;
    // SWAR prefix scan covers short fields (typical CSV) without paying NEON setup cost.
    size_t prefix_end = s < SONICSV_CACHE_LINE_SIZE ? s : SONICSV_CACHE_LINE_SIZE;
    for (; i + 8 <= prefix_end; i += 8) {
        uint64_t chunk;
        memcpy(&chunk, d + i, 8);

        uint64_t combined = SWAR_HAS_ZERO(chunk ^ bc_delim) |
                            SWAR_HAS_ZERO(chunk ^ bc_nl) |
                            SWAR_HAS_ZERO(chunk ^ bc_cr);
        if (combined) {
            int pos_offset = csv_swar_find_first(combined);
            return d + i + pos_offset;
        }
    }
    for (; i < prefix_end; i++) {
        char c = d[i];
        if (c == delim || c == '\n' || c == '\r') return d + i;
    }

#ifdef HAVE_NEON
    if (sonicsv_likely(s - i >= 16)) {
        const size_t simd_size = s - i;
        const char *found = csv_neon_find_delim_or_newline(d + i, simd_size, delim);
        if (found) return found;
        i += simd_size & ~(size_t)15;
    }
#endif

    for (; i + 8 <= s; i += 8) {
        uint64_t chunk;
        memcpy(&chunk, d + i, 8);

        uint64_t combined = SWAR_HAS_ZERO(chunk ^ bc_delim) |
                            SWAR_HAS_ZERO(chunk ^ bc_nl) |
                            SWAR_HAS_ZERO(chunk ^ bc_cr);
        if (combined) {
            int pos_offset = csv_swar_find_first(combined);
            return d + i + pos_offset;
        }
    }

    for (; i < s; i++) {
        char c = d[i];
        if (c == delim || c == '\n' || c == '\r') return d + i;
    }

    return NULL;
#else
    // Check the first cache line with low-overhead SWAR before paying the
    // setup cost for wider SIMD scans.
    size_t prefix_end = s < SONICSV_CACHE_LINE_SIZE ? s : SONICSV_CACHE_LINE_SIZE;
    for (; i + 8 <= prefix_end; i += 8) {
        uint64_t chunk;
        memcpy(&chunk, d + i, 8);

        uint64_t combined = SWAR_HAS_ZERO(chunk ^ bc_delim) |
                            SWAR_HAS_ZERO(chunk ^ bc_nl) |
                            SWAR_HAS_ZERO(chunk ^ bc_cr);
        if (combined) {
            int pos_offset = csv_swar_find_first(combined);
            return d + i + pos_offset;
        }
    }
    for (; i < prefix_end; i++) {
        char c = d[i];
        if (c == delim || c == '\n' || c == '\r') return d + i;
    }

#ifdef HAVE_AVX512
    if (sonicsv_likely(features & CSV_SIMD_AVX512)) {
        const size_t simd_size = s - i;
        const char *found = csv_avx512_find_delim_or_newline(d + i, simd_size, delim);
        if (found) return found;
        i += simd_size & ~(size_t)63;
    } else
#endif
#ifdef HAVE_AVX2
    if (sonicsv_likely(features & CSV_SIMD_AVX2)) {
        const size_t simd_size = s - i;
        const char *found = csv_avx2_find_delim_or_newline(d + i, simd_size, delim);
        if (found) return found;
        i += simd_size & ~(size_t)31;
    } else
#endif
#ifdef HAVE_SSE4_2
    if (sonicsv_likely(features & CSV_SIMD_SSE4_2)) {
        const size_t simd_size = s - i;
        const char *found = csv_sse42_find_delim_or_newline(d + i, simd_size, delim);
        if (found) return found;
        i += simd_size & ~(size_t)15;
    }
#endif

    for (; i + 8 <= s; i += 8) {
        uint64_t chunk;
        memcpy(&chunk, d + i, 8);

        uint64_t combined = SWAR_HAS_ZERO(chunk ^ bc_delim) |
                            SWAR_HAS_ZERO(chunk ^ bc_nl) |
                            SWAR_HAS_ZERO(chunk ^ bc_cr);
        if (combined) {
            int pos_offset = csv_swar_find_first(combined);
            return d + i + pos_offset;
        }
    }

    for (; i < s; i++) {
        char c = d[i];
        if (c == delim || c == '\n' || c == '\r') return d + i;
    }

    return NULL;
#endif
#endif
}

// Field/row emit helpers shared by simple-path bitmask variants. Inline so the
// compiler can fuse them into each platform-specific bitmask loop.
static sonicsv_force_inline csv_error_t csv_emit_unquoted_field(
    csv_parser_t *p, const char *field_start, size_t fsize, size_t max_field) {
    if (sonicsv_unlikely(fsize > max_field))
        return report_error(p, CSV_ERROR_FIELD_TOO_LARGE, "Field size exceeds max_field_size");
    if (sonicsv_unlikely(p->num_fields >= p->fields_capacity)) {
        if (ensure_capacity((void**)&p->fields, &p->fields_capacity,
                            p->num_fields + 1, sizeof(csv_field_t), p) != CSV_OK)
            return CSV_ERROR_OUT_OF_MEMORY;
    }
    csv_field_t *f = &p->fields[p->num_fields++];
    f->data = field_start;
    f->size = fsize;
    f->quoted = false;
    return CSV_OK;
}

static sonicsv_force_inline csv_error_t csv_emit_unquoted_row(
    csv_parser_t *p, const char *buf, const char *row_start, const char *sep, size_t max_row) {
    // sep points at the newline (\r or \n), exclusive. Number of delimiters in
    // the row is num_fields - 1, so total field bytes = (sep - row_start) - (num_fields - 1).
    size_t row_size = (size_t)(sep - row_start) - (p->num_fields - 1);
    // ignore_empty_lines: a "blank line" arrives here as one zero-byte field
    // (e.g., bare `\n` produced num_fields=1, row_size=0). The slow-path's
    // finish_row applies the same heuristic.
    if (p->options.ignore_empty_lines &&
        p->num_fields == 1 && p->fields[0].size == 0 && !p->fields[0].quoted) {
        p->num_fields = 0;
        return CSV_OK;
    }
    if (sonicsv_unlikely(row_size > max_row))
        return report_error(p, CSV_ERROR_ROW_TOO_LARGE, "Row size exceeds max_row_size");
    p->stats.total_rows_parsed++;
    p->stats.total_fields_parsed += p->num_fields;
    p->sum_field_size += row_size;
    p->sum_row_size += row_size;
    if (p->row_callback) {
        csv_row_t row = {p->fields, p->num_fields,
                         p->stats.total_rows_parsed, (size_t)(row_start - buf)};
        p->row_callback(&row, p->row_callback_data);
    }
    p->num_fields = 0;
    return CSV_OK;
}

// Bitmask-based fast-path: rather than calling find_delim_or_newline once per
// field (each call paying SIMD setup), we generate a bitmask of separator
// positions for an entire chunk in a single SIMD op, then iterate set bits.
// For typical CSVs (~10–30 byte fields), this amortizes SIMD setup across
// many fields and is the dominant performance optimization for the simple path.
//
// The loop is structured so a field that crosses chunk boundaries is handled
// transparently: field_start simply remains pointing into an earlier chunk
// until we hit its terminating separator. CRLF crossing chunk boundaries is
// handled via the `sep < field_start` guard — after consuming `\r\n`, we
// advance field_start past the `\n`, and the next chunk's mask bit for that
// `\n` is silently skipped.

#ifdef HAVE_NEON
#define CSV_BITMASK_CHUNK 16
static sonicsv_force_inline uint16_t csv_neon_sep_bits16(const uint8_t *d, uint8_t delim) {
    static const uint8_t bit_lookup[16] sonicsv_aligned(16) = {
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
    };
    const uint8x16_t v_delim = vdupq_n_u8(delim);
    const uint8x16_t v_nl = vdupq_n_u8('\n');
    const uint8x16_t v_cr = vdupq_n_u8('\r');
    uint8x16_t chunk = vld1q_u8(d);
    uint8x16_t cmp = vorrq_u8(vorrq_u8(vceqq_u8(chunk, v_delim), vceqq_u8(chunk, v_nl)),
                              vceqq_u8(chunk, v_cr));
    uint8x16_t bits = vandq_u8(cmp, vld1q_u8(bit_lookup));
    uint8_t lo = vaddv_u8(vget_low_u8(bits));
    uint8_t hi = vaddv_u8(vget_high_u8(bits));
    return (uint16_t)((uint16_t)hi << 8) | (uint16_t)lo;
}

static sonicsv_hot csv_error_t csv_parse_simple_bitmask_neon(csv_parser_t *p, const char *buf, size_t sz) {
    const char delim = p->options.delimiter;
    const size_t max_field = p->options.max_field_size;
    const size_t max_row = p->options.max_row_size;
    const char *end = buf + sz;
    const char *row_start = buf;
    const char *field_start = buf;
    const char *pos = buf;

    if (p->fields_capacity < 64) {
        if (ensure_capacity((void**)&p->fields, &p->fields_capacity, 64,
                            sizeof(csv_field_t), p) != CSV_OK)
            return CSV_ERROR_OUT_OF_MEMORY;
    }

    while (pos + CSV_BITMASK_CHUNK <= end) {
        uint32_t mask = csv_neon_sep_bits16((const uint8_t*)pos, (uint8_t)delim);

        while (mask) {
            int byte_off = sonicsv_ctz32(mask);
            mask &= mask - 1;

            const char *sep = pos + byte_off;
            if (sonicsv_unlikely(sep < field_start)) continue;  // skipped LF after CRLF

            char c = *sep;
            size_t fsize = (size_t)(sep - field_start);
            csv_error_t err = csv_emit_unquoted_field(p, field_start, fsize, max_field);
            if (sonicsv_unlikely(err != CSV_OK)) return err;

            if (sonicsv_likely(c == delim)) {
                field_start = sep + 1;
            } else {
                err = csv_emit_unquoted_row(p, buf, row_start, sep, max_row);
                if (sonicsv_unlikely(err != CSV_OK)) return err;
                const char *next = sep + 1;
                if (c == '\r' && next < end && *next == '\n') next++;
                row_start = next;
                field_start = next;
            }
        }
        pos += CSV_BITMASK_CHUNK;
    }

    // Tail handled by scalar walker — avoids duplicating edge-case logic for
    // sub-chunk remainders.
    // CRLF straddling the chunk/tail boundary (\r at the last byte of the
    // last chunk, \n at offset 0 of the tail) leaves field_start advanced
    // past pos. Re-sync pos so the tail's find() doesn't re-discover the
    // already-consumed \n and underflow `found - field_start`.
    if (field_start > pos) pos = field_start;

    while (pos < end) {
        const char *found = csv_find_delim_or_newline(pos, (size_t)(end - pos), delim, p->simd_features);
        if (!found) {
            // No more separators in the tail — handled by post-loop trailing-emit.
            pos = end;
            break;
        }

        char c = *found;
        size_t fsize = (size_t)(found - field_start);
        csv_error_t err = csv_emit_unquoted_field(p, field_start, fsize, max_field);
        if (sonicsv_unlikely(err != CSV_OK)) return err;

        if (c == delim) {
            field_start = found + 1;
            pos = field_start;
        } else {
            err = csv_emit_unquoted_row(p, buf, row_start, found, max_row);
            if (sonicsv_unlikely(err != CSV_OK)) return err;
            const char *next = found + 1;
            if (c == '\r' && next < end && *next == '\n') next++;
            row_start = next;
            field_start = next;
            pos = next;
        }
    }

    if (field_start < end) {
        csv_error_t err = csv_emit_unquoted_field(p, field_start, (size_t)(end - field_start), max_field);
        if (sonicsv_unlikely(err != CSV_OK)) return err;
    }
    if (p->num_fields > 0) {
        csv_error_t err = csv_emit_unquoted_row(p, buf, row_start, end, max_row);
        if (sonicsv_unlikely(err != CSV_OK)) return err;
    }

    p->stats.total_bytes_processed += sz;
    return CSV_OK;
}
#undef CSV_BITMASK_CHUNK
#endif

#ifdef HAVE_AVX2
SONICSV_TARGET("avx2")
static sonicsv_hot csv_error_t csv_parse_simple_bitmask_avx2(csv_parser_t *p, const char *buf, size_t sz) {
    const char delim = p->options.delimiter;
    const size_t max_field = p->options.max_field_size;
    const size_t max_row = p->options.max_row_size;
    const char *end = buf + sz;
    const char *row_start = buf;
    const char *field_start = buf;
    const char *pos = buf;

    if (p->fields_capacity < 64) {
        if (ensure_capacity((void**)&p->fields, &p->fields_capacity, 64,
                            sizeof(csv_field_t), p) != CSV_OK)
            return CSV_ERROR_OUT_OF_MEMORY;
    }

    const __m256i v_delim = _mm256_set1_epi8(delim);
    const __m256i v_nl = _mm256_set1_epi8('\n');
    const __m256i v_cr = _mm256_set1_epi8('\r');

    while (pos + 32 <= end) {
        __m256i chunk = _mm256_loadu_si256((const __m256i*)pos);
        __m256i cmp = _mm256_or_si256(_mm256_or_si256(_mm256_cmpeq_epi8(chunk, v_delim),
                                                      _mm256_cmpeq_epi8(chunk, v_nl)),
                                      _mm256_cmpeq_epi8(chunk, v_cr));
        uint32_t mask = (uint32_t)_mm256_movemask_epi8(cmp);

        while (mask) {
            int byte_off = sonicsv_ctz32(mask);
            mask &= mask - 1;

            const char *sep = pos + byte_off;
            if (sonicsv_unlikely(sep < field_start)) continue;

            char c = *sep;
            size_t fsize = (size_t)(sep - field_start);
            csv_error_t err = csv_emit_unquoted_field(p, field_start, fsize, max_field);
            if (sonicsv_unlikely(err != CSV_OK)) return err;

            if (sonicsv_likely(c == delim)) {
                field_start = sep + 1;
            } else {
                err = csv_emit_unquoted_row(p, buf, row_start, sep, max_row);
                if (sonicsv_unlikely(err != CSV_OK)) return err;
                const char *next = sep + 1;
                if (c == '\r' && next < end && *next == '\n') next++;
                row_start = next;
                field_start = next;
            }
        }
        pos += 32;
    }

    // Re-sync after CRLF straddling the chunk/tail boundary.
    if (field_start > pos) pos = field_start;

    while (pos < end) {
        const char *found = csv_find_delim_or_newline(pos, (size_t)(end - pos), delim, p->simd_features);
        if (!found) { pos = end; break; }
        char c = *found;
        size_t fsize = (size_t)(found - field_start);
        csv_error_t err = csv_emit_unquoted_field(p, field_start, fsize, max_field);
        if (sonicsv_unlikely(err != CSV_OK)) return err;

        if (c == delim) {
            field_start = found + 1;
            pos = field_start;
        } else {
            err = csv_emit_unquoted_row(p, buf, row_start, found, max_row);
            if (sonicsv_unlikely(err != CSV_OK)) return err;
            const char *next = found + 1;
            if (c == '\r' && next < end && *next == '\n') next++;
            row_start = next;
            field_start = next;
            pos = next;
        }
    }

    if (field_start < end) {
        csv_error_t err = csv_emit_unquoted_field(p, field_start, (size_t)(end - field_start), max_field);
        if (sonicsv_unlikely(err != CSV_OK)) return err;
    }
    if (p->num_fields > 0) {
        csv_error_t err = csv_emit_unquoted_row(p, buf, row_start, end, max_row);
        if (sonicsv_unlikely(err != CSV_OK)) return err;
    }

    p->stats.total_bytes_processed += sz;
    return CSV_OK;
}
#endif

#ifdef HAVE_SSE4_2
SONICSV_TARGET("sse4.2")
static sonicsv_hot csv_error_t csv_parse_simple_bitmask_sse42(csv_parser_t *p, const char *buf, size_t sz) {
    const char delim = p->options.delimiter;
    const size_t max_field = p->options.max_field_size;
    const size_t max_row = p->options.max_row_size;
    const char *end = buf + sz;
    const char *row_start = buf;
    const char *field_start = buf;
    const char *pos = buf;

    if (p->fields_capacity < 64) {
        if (ensure_capacity((void**)&p->fields, &p->fields_capacity, 64,
                            sizeof(csv_field_t), p) != CSV_OK)
            return CSV_ERROR_OUT_OF_MEMORY;
    }

    const __m128i v_delim = _mm_set1_epi8(delim);
    const __m128i v_nl = _mm_set1_epi8('\n');
    const __m128i v_cr = _mm_set1_epi8('\r');

    while (pos + 16 <= end) {
        __m128i chunk = _mm_loadu_si128((const __m128i*)pos);
        __m128i cmp = _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(chunk, v_delim),
                                                _mm_cmpeq_epi8(chunk, v_nl)),
                                   _mm_cmpeq_epi8(chunk, v_cr));
        uint32_t mask = (uint32_t)_mm_movemask_epi8(cmp);

        while (mask) {
            int byte_off = sonicsv_ctz32(mask);
            mask &= mask - 1;

            const char *sep = pos + byte_off;
            if (sonicsv_unlikely(sep < field_start)) continue;

            char c = *sep;
            size_t fsize = (size_t)(sep - field_start);
            csv_error_t err = csv_emit_unquoted_field(p, field_start, fsize, max_field);
            if (sonicsv_unlikely(err != CSV_OK)) return err;

            if (sonicsv_likely(c == delim)) {
                field_start = sep + 1;
            } else {
                err = csv_emit_unquoted_row(p, buf, row_start, sep, max_row);
                if (sonicsv_unlikely(err != CSV_OK)) return err;
                const char *next = sep + 1;
                if (c == '\r' && next < end && *next == '\n') next++;
                row_start = next;
                field_start = next;
            }
        }
        pos += 16;
    }

    // Re-sync after CRLF straddling the chunk/tail boundary.
    if (field_start > pos) pos = field_start;

    while (pos < end) {
        const char *found = csv_find_delim_or_newline(pos, (size_t)(end - pos), delim, p->simd_features);
        if (!found) { pos = end; break; }
        char c = *found;
        size_t fsize = (size_t)(found - field_start);
        csv_error_t err = csv_emit_unquoted_field(p, field_start, fsize, max_field);
        if (sonicsv_unlikely(err != CSV_OK)) return err;

        if (c == delim) {
            field_start = found + 1;
            pos = field_start;
        } else {
            err = csv_emit_unquoted_row(p, buf, row_start, found, max_row);
            if (sonicsv_unlikely(err != CSV_OK)) return err;
            const char *next = found + 1;
            if (c == '\r' && next < end && *next == '\n') next++;
            row_start = next;
            field_start = next;
            pos = next;
        }
    }

    if (field_start < end) {
        csv_error_t err = csv_emit_unquoted_field(p, field_start, (size_t)(end - field_start), max_field);
        if (sonicsv_unlikely(err != CSV_OK)) return err;
    }
    if (p->num_fields > 0) {
        csv_error_t err = csv_emit_unquoted_row(p, buf, row_start, end, max_row);
        if (sonicsv_unlikely(err != CSV_OK)) return err;
    }

    p->stats.total_bytes_processed += sz;
    return CSV_OK;
}
#endif

// Fast-path parser for quote-free buffers. The caller (csv_parse_buffer) does
// one upfront SIMD pass over the whole buffer to confirm no quote character
// is present, which lets this loop use the cheap 3-char scan and skip the
// per-iteration quote check entirely. max_row_size is enforced once per row
// rather than per field — measurable savings on wide CSVs.
static sonicsv_hot csv_error_t csv_parse_simple_fast(csv_parser_t *p, const char *buf, size_t sz) {
    // Dispatch to bitmask parser when SIMD is available — amortizes SIMD setup
    // across all separators in a chunk instead of paying it per field.
#ifdef HAVE_AVX2
    if (sonicsv_likely(p->simd_features & CSV_SIMD_AVX2) && sz >= 32) {
        return csv_parse_simple_bitmask_avx2(p, buf, sz);
    }
#endif
#ifdef HAVE_SSE4_2
    if (sonicsv_likely(p->simd_features & CSV_SIMD_SSE4_2) && sz >= 16) {
        return csv_parse_simple_bitmask_sse42(p, buf, sz);
    }
#endif
#ifdef HAVE_NEON
    if (sonicsv_likely(p->simd_features & CSV_SIMD_NEON) && sz >= 16) {
        return csv_parse_simple_bitmask_neon(p, buf, sz);
    }
#endif

    // Scalar/SWAR fallback — same algorithm, find_delim_or_newline per field.

    const char delimiter = p->options.delimiter;
    const size_t max_field_size = p->options.max_field_size;
    const size_t max_row_size = p->options.max_row_size;
    const char *pos = buf;
    const char *end = buf + sz;
    const char *row_start = pos;
    const char *field_start = pos;

    if (p->fields_capacity < 64) {
        if (ensure_capacity((void**)&p->fields, &p->fields_capacity, 64, sizeof(csv_field_t), p) != CSV_OK)
            return CSV_ERROR_OUT_OF_MEMORY;
    }

    while (pos < end) {
        const char *found = csv_find_delim_or_newline(pos, (size_t)(end - pos), delimiter, p->simd_features);

        if (!found) {
            size_t field_size = (size_t)(end - field_start);
            if (sonicsv_unlikely(field_size > max_field_size))
                return report_error(p, CSV_ERROR_FIELD_TOO_LARGE, "Field size exceeds max_field_size");
            if (sonicsv_unlikely(p->num_fields >= p->fields_capacity)) {
                if (ensure_capacity((void**)&p->fields, &p->fields_capacity, p->num_fields + 1, sizeof(csv_field_t), p) != CSV_OK)
                    return CSV_ERROR_OUT_OF_MEMORY;
            }
            p->fields[p->num_fields].data = field_start;
            p->fields[p->num_fields].size = field_size;
            p->fields[p->num_fields].quoted = false;
            p->num_fields++;

            size_t row_size = (size_t)(end - row_start) - (p->num_fields - 1);
            if (sonicsv_unlikely(row_size > max_row_size))
                return report_error(p, CSV_ERROR_ROW_TOO_LARGE, "Row size exceeds max_row_size");
            p->stats.total_rows_parsed++;
            p->stats.total_fields_parsed += p->num_fields;
            p->sum_field_size += row_size;
            p->sum_row_size   += row_size;
            if (p->row_callback) {
                csv_row_t row = {p->fields, p->num_fields, p->stats.total_rows_parsed, (size_t)(row_start - buf)};
                p->row_callback(&row, p->row_callback_data);
            }
            p->num_fields = 0;
            break;
        }

        char c = *found;
        pos = found;

        size_t field_size = (size_t)(pos - field_start);
        if (sonicsv_unlikely(field_size > max_field_size))
            return report_error(p, CSV_ERROR_FIELD_TOO_LARGE, "Field size exceeds max_field_size");

        if (sonicsv_unlikely(p->num_fields >= p->fields_capacity)) {
            if (ensure_capacity((void**)&p->fields, &p->fields_capacity, p->num_fields + 1, sizeof(csv_field_t), p) != CSV_OK)
                return CSV_ERROR_OUT_OF_MEMORY;
        }
        p->fields[p->num_fields].data = field_start;
        p->fields[p->num_fields].size = field_size;
        p->fields[p->num_fields].quoted = false;
        p->num_fields++;

        if (c == delimiter) {
            pos++;
            field_start = pos;
        } else { // '\n' or '\r'
            size_t row_size = (size_t)(pos - row_start) - (p->num_fields - 1);
            if (sonicsv_unlikely(row_size > max_row_size))
                return report_error(p, CSV_ERROR_ROW_TOO_LARGE, "Row size exceeds max_row_size");
            p->stats.total_rows_parsed++;
            p->stats.total_fields_parsed += p->num_fields;
            p->sum_field_size += row_size;
            p->sum_row_size   += row_size;
            if (p->row_callback) {
                csv_row_t row = {p->fields, p->num_fields, p->stats.total_rows_parsed, (size_t)(row_start - buf)};
                p->row_callback(&row, p->row_callback_data);
            }
            p->num_fields = 0;

            pos++;
            if (c == '\r' && pos < end && *pos == '\n') pos++;
            row_start = pos;
            field_start = pos;
        }
    }

    p->stats.total_bytes_processed += sz;
    return CSV_OK;
}

// --- FIXED CSV Parsing Logic ---
csv_error_t csv_parse_buffer(csv_parser_t *p, const char *buf, size_t sz, bool is_final) {
  // Enhanced parameter validation
  if (!p) return CSV_ERROR_INVALID_ARGS;
  if (sz > 0 && !buf) return CSV_ERROR_INVALID_ARGS;
  if (sz > SIZE_MAX / 2) return CSV_ERROR_INVALID_ARGS; // Prevent overflow
  if (p->options.max_field_size == 0) return CSV_ERROR_INVALID_ARGS;
  if (p->options.max_row_size == 0) return CSV_ERROR_INVALID_ARGS;

  // CRLF straddle: a `\r` was consumed as a row terminator at the very end
  // of the previous chunk. If this chunk starts with `\n`, it's the second
  // half of the same CRLF and must be skipped — otherwise the parser would
  // emit a spurious empty row for the lone `\n`.
  if (sonicsv_unlikely(p->pending_lf_skip)) {
    p->pending_lf_skip = false;
    if (sz > 0 && buf[0] == '\n') {
      buf += 1;
      sz -= 1;
      p->current_row_start_offset += 1;
      p->stats.total_bytes_processed += 1;
    }
  }

  // Skip UTF-8 BOM at the very beginning of input (first call with no prior data)
  if (sz >= 3 && p->stats.total_bytes_processed == 0 && p->unparsed_size == 0) {
    if (memcmp(buf, CSV_UTF8_BOM, 3) == 0) {
      buf += 3;
      sz -= 3;
      p->current_row_start_offset = 3; // Adjust offset for BOM
    }
  }

  // Fast-path: one upfront SIMD pass over the whole buffer to confirm there
  // are no quote characters anywhere; if so, dispatch to the lean simple-fast
  // parser. The previous 4 KB sample heuristic was unsafe — it mis-parsed
  // files whose quotes appeared past byte 4096. The full SIMD scan runs at
  // ~15+ GB/s on NEON / AVX2, far cheaper than the per-iteration tax of
  // adding a quote-check to the inner scan loop.
  if (is_final && p->state == CSV_STATE_FIELD_START && p->unparsed_size == 0 && sz > 64 &&
      !p->options.trim_whitespace && !p->options.strict_mode) {
    if (csv_find_single_char(buf, sz, p->options.quote_char) == NULL) {
      return csv_parse_simple_fast(p, buf, sz);
    }
  }

  // Handle unparsed data from previous call.
  //
  // If a row was partially emitted on the previous call, p->fields[] contains
  // entries whose `data` references unparsed_buffer. Growing the buffer here
  // may relocate it; ensure_capacity_relocate_fields fixes those pointers
  // along with the move.
   if (p->unparsed_size > 0) {
     if (sonicsv_unlikely(ensure_capacity_relocate_fields(&p->unparsed_buffer,
                                                          &p->unparsed_capacity,
                                                          p->unparsed_size + sz, p) != CSV_OK))
       return CSV_ERROR_OUT_OF_MEMORY;
     memcpy(p->unparsed_buffer + p->unparsed_size, buf, sz);
     p->unparsed_size += sz;
     buf = p->unparsed_buffer;
     sz = p->unparsed_size;
   }
 
   const char *pos = buf, *end = buf + sz;
   csv_error_t err = CSV_OK;
   const char delimiter = p->options.delimiter, quote_char = p->options.quote_char;
   size_t original_bytes_processed = p->stats.total_bytes_processed;
 
   // Optimized main parsing loop with better branch prediction
   while (pos < end && sonicsv_likely(err == CSV_OK)) {
     char c = *pos;
 
     // Use computed goto for better branch prediction (if supported)
     switch (p->state) {
       case CSV_STATE_FIELD_START:
         // Optimize common case: unquoted field
         if (sonicsv_likely(c != quote_char && c != delimiter && c != '\n' && c != '\r')) {
           // Fast path: start of unquoted field - use SIMD to find end.
           //
           // In non-strict mode (the default), a quote character appearing
           // mid-unquoted-field has no semantic meaning — it's just a literal
           // byte. Searching for it adds a 4th comparison to the SIMD inner
           // loop and produces a non-conventional split (e.g., `abc"def`
           // becoming two fields). Skipping the quote in the search avoids
           // both. In strict mode, we still scan for it so the parser can
           // raise an error.
           const char *field_start = pos;
           csv_search_result_t res;
           if (sonicsv_unlikely(p->options.strict_mode)) {
             res = csv_find_special_char_with_parser(p, pos, end - pos, delimiter, quote_char, '\n', '\r');
           } else {
             const char *found = csv_find_delim_or_newline(pos, end - pos, delimiter, p->simd_features);
             res.pos = found;
             res.offset = found ? (size_t)(found - pos) : (size_t)(end - pos);
           }
 
           if (res.pos == NULL) {
             if (is_final) {
               // Add final field and finish row
               if (sonicsv_unlikely((err = add_field(p, field_start, end - field_start, false)) != CSV_OK)) break;
               pos = end;
               if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
             } else {
               // Need more data - save state. Stabilize any pending fields
               // whose `data` lies in the buffer we're about to mutate, so
               // their content survives the memmove/realloc. Two distinct
               // buffers may need protection:
               //   - `buf` (the source we're saving from — either
               //     unparsed_buffer about to be memmove'd, or the user's
               //     chunk that goes out of scope on return)
               //   - `unparsed_buffer` itself when `buf` is the user's chunk:
               //     its contents are about to be overwritten by the memcpy.
               size_t consumed = field_start - buf;
               size_t remaining = end - field_start;

               if (sonicsv_unlikely((err = csv_stabilize_pending_fields(p, buf, end)) != CSV_OK))
                 return err;
               if (buf != p->unparsed_buffer && p->unparsed_buffer) {
                 if (sonicsv_unlikely((err = csv_stabilize_pending_fields(p, p->unparsed_buffer,
                                                                          p->unparsed_buffer + p->unparsed_capacity)) != CSV_OK))
                   return err;
               }

               if (buf == p->unparsed_buffer) {
                 memmove(p->unparsed_buffer, field_start, remaining);
               } else {
                 if (sonicsv_unlikely(ensure_capacity_relocate_fields(&p->unparsed_buffer,
                                                                      &p->unparsed_capacity,
                                                                      remaining, p) != CSV_OK))
                   return CSV_ERROR_OUT_OF_MEMORY;
                 memcpy(p->unparsed_buffer, field_start, remaining);
               }
               p->unparsed_size = remaining;
               p->stats.total_bytes_processed = original_bytes_processed + consumed;
               return CSV_OK;
             }
           } else {
             pos = res.pos;
 
             // Add the field
             if (sonicsv_unlikely((err = add_field(p, field_start, pos - field_start, false)) != CSV_OK)) break;
 
             // Handle what we found
             c = *pos;
             if (c == delimiter) {
               pos++;
             } else if (c == '\n') {
               pos++;
               if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
               p->current_row_start_offset = original_bytes_processed + (pos - buf);
             } else if (c == '\r') {
               pos++;
               if (pos < end && *pos == '\n') pos++; // Skip LF in CRLF
               else if (pos == end && !is_final) p->pending_lf_skip = true;
               if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
               p->current_row_start_offset = original_bytes_processed + (pos - buf);
             } else if (c == quote_char && p->options.strict_mode) {
               err = report_error(p, CSV_ERROR_PARSE_ERROR, "Quote character in unquoted field");
               break;
             }
           }
         } else if (c == quote_char) {
           p->state = CSV_STATE_IN_QUOTED_FIELD;
           p->field_buffer_pos = 0;
           pos++;
         } else if (c == delimiter) {
           if (sonicsv_unlikely((err = add_field(p, "", 0, false)) != CSV_OK)) break;
           pos++;
         } else if (c == '\n') {
           if (sonicsv_unlikely((err = add_field(p, "", 0, false)) != CSV_OK)) break;
           if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
           p->current_row_start_offset = original_bytes_processed + (pos + 1 - buf);
           pos++;
         } else if (c == '\r') {
           if (sonicsv_unlikely((err = add_field(p, "", 0, false)) != CSV_OK)) break;
           pos++;
           if (pos < end && *pos == '\n') pos++; // Skip LF in CRLF
           else if (pos == end && !is_final) p->pending_lf_skip = true;
           if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
           p->current_row_start_offset = original_bytes_processed + (pos - buf);
         }
         break;
 
       case CSV_STATE_IN_QUOTED_FIELD:
         {
           // Defer the copy: as long as field_buffer is empty (no `""` escape
           // hit yet) we can keep the field as a slice of the input buffer
           // and emit it via add_field_ref, skipping both the field_buffer
           // round-trip and the field_data_pool memcpy that add_field would
           // do for q=true. The input buffer (mmap'd file or parser-owned
           // unparsed_buffer) is stable for the duration of the row callback.
           const char *content_start = pos;
           const char *quote_pos = csv_find_single_char(pos, end - pos, quote_char);

           if (sonicsv_unlikely(!quote_pos)) {
             if (is_final) {
               if (sonicsv_unlikely(p->options.strict_mode)) {
                 err = report_error(p, CSV_ERROR_PARSE_ERROR, "Unclosed quoted field");
                 break;
               }
               // Non-strict: accept the unclosed field as-is.
               if (p->field_buffer_pos == 0) {
                 if (sonicsv_unlikely((err = add_field_ref(p, content_start, end - content_start, true)) != CSV_OK)) break;
               } else {
                 if (end > content_start) {
                   if (sonicsv_unlikely((err = append_to_field_buffer(p, content_start, end - content_start)) != CSV_OK)) break;
                 }
                 if (sonicsv_unlikely((err = add_field(p, p->field_buffer, p->field_buffer_pos, true)) != CSV_OK)) break;
                 p->field_buffer_pos = 0;
               }
               if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
               pos = end;
             } else {
               // Streaming: the source buffer is about to be invalidated, so
               // any held-back zero-copy slice must be flushed into the
               // parser-owned unparsed_buffer. Pending fields previously
               // emitted via add_field_ref also reference this buffer and
               // must be stabilized before the memmove/realloc. See the
               // FIELD_START partial-save path above for the two-range
               // rationale.
               size_t consumed = content_start - buf;
               size_t remaining = end - content_start;

               if (sonicsv_unlikely((err = csv_stabilize_pending_fields(p, buf, end)) != CSV_OK))
                 return err;
               if (buf != p->unparsed_buffer && p->unparsed_buffer) {
                 if (sonicsv_unlikely((err = csv_stabilize_pending_fields(p, p->unparsed_buffer,
                                                                          p->unparsed_buffer + p->unparsed_capacity)) != CSV_OK))
                   return err;
               }

               if (sonicsv_likely(buf == p->unparsed_buffer)) {
                 memmove(p->unparsed_buffer, content_start, remaining);
               } else {
                 if (sonicsv_unlikely(ensure_capacity_relocate_fields(&p->unparsed_buffer,
                                                                      &p->unparsed_capacity,
                                                                      remaining, p) != CSV_OK))
                   return CSV_ERROR_OUT_OF_MEMORY;
                 memcpy(p->unparsed_buffer, content_start, remaining);
               }
               p->unparsed_size = remaining;
               p->stats.total_bytes_processed = original_bytes_processed + consumed;
               return CSV_OK;
             }
             break;
           }

           // `""` escape: flush content_start..quote_pos+1 (keeps one of the
           // pair) into field_buffer; once we do this, zero-copy is off for
           // this field.
           if (sonicsv_unlikely(p->options.double_quote && quote_pos + 1 < end && quote_pos[1] == quote_char)) {
             if (sonicsv_unlikely((err = append_to_field_buffer(p, content_start, quote_pos - content_start + 1)) != CSV_OK)) break;
             pos = quote_pos + 2;
             break;  // remain in IN_QUOTED_FIELD; outer loop continues scanning
           }

           // Closing quote. Look ahead one byte to keep the common
           // quote-then-delim/newline case eagerly emittable (zero-copy when
           // field_buffer is empty). Whitespace, malformed input, or
           // end-of-buffer-not-final fall through to QUOTE_IN_QUOTED_FIELD
           // for resolution and require the data to be in field_buffer.
           const char *qend = quote_pos;
           const char *next = quote_pos + 1;
           const bool zerocopy = (p->field_buffer_pos == 0);
           const size_t qsize = (size_t)(qend - content_start);

           if (next < end) {
             char nc = *next;
             if (sonicsv_likely(nc == delimiter)) {
               if (zerocopy) {
                 if (sonicsv_unlikely((err = add_field_ref(p, content_start, qsize, true)) != CSV_OK)) break;
               } else {
                 if (qsize > 0) { if (sonicsv_unlikely((err = append_to_field_buffer(p, content_start, qsize)) != CSV_OK)) break; }
                 if (sonicsv_unlikely((err = add_field(p, p->field_buffer, p->field_buffer_pos, true)) != CSV_OK)) break;
                 p->field_buffer_pos = 0;
               }
               pos = next + 1;
               p->state = CSV_STATE_FIELD_START;
               break;
             }
             if (nc == '\n') {
               if (zerocopy) {
                 if (sonicsv_unlikely((err = add_field_ref(p, content_start, qsize, true)) != CSV_OK)) break;
               } else {
                 if (qsize > 0) { if (sonicsv_unlikely((err = append_to_field_buffer(p, content_start, qsize)) != CSV_OK)) break; }
                 if (sonicsv_unlikely((err = add_field(p, p->field_buffer, p->field_buffer_pos, true)) != CSV_OK)) break;
                 p->field_buffer_pos = 0;
               }
               pos = next + 1;
               if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
               p->state = CSV_STATE_FIELD_START;
               p->current_row_start_offset = original_bytes_processed + (pos - buf);
               break;
             }
             if (nc == '\r') {
               if (zerocopy) {
                 if (sonicsv_unlikely((err = add_field_ref(p, content_start, qsize, true)) != CSV_OK)) break;
               } else {
                 if (qsize > 0) { if (sonicsv_unlikely((err = append_to_field_buffer(p, content_start, qsize)) != CSV_OK)) break; }
                 if (sonicsv_unlikely((err = add_field(p, p->field_buffer, p->field_buffer_pos, true)) != CSV_OK)) break;
                 p->field_buffer_pos = 0;
               }
               pos = next + 1;
               if (pos < end && *pos == '\n') pos++;
               else if (pos == end && !is_final) p->pending_lf_skip = true;
               if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
               p->state = CSV_STATE_FIELD_START;
               p->current_row_start_offset = original_bytes_processed + (pos - buf);
               break;
             }
           } else if (is_final) {
             // Closing quote is the last byte of input.
             if (zerocopy) {
               if (sonicsv_unlikely((err = add_field_ref(p, content_start, qsize, true)) != CSV_OK)) break;
             } else {
               if (qsize > 0) { if (sonicsv_unlikely((err = append_to_field_buffer(p, content_start, qsize)) != CSV_OK)) break; }
               if (sonicsv_unlikely((err = add_field(p, p->field_buffer, p->field_buffer_pos, true)) != CSV_OK)) break;
               p->field_buffer_pos = 0;
             }
             if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
             pos = end;
             p->state = CSV_STATE_FIELD_START;
             break;
           }

           // Rare disposition (whitespace, malformed, or buffer-boundary
           // continuation): flush content into field_buffer and let
           // QUOTE_IN_QUOTED_FIELD resolve it. From this point on the field
           // is no longer eligible for zero-copy.
           if (qsize > 0) {
             if (sonicsv_unlikely((err = append_to_field_buffer(p, content_start, qsize)) != CSV_OK)) break;
           }
           pos = next;
           p->state = CSV_STATE_QUOTE_IN_QUOTED_FIELD;
         }
         break;
 
       case CSV_STATE_QUOTE_IN_QUOTED_FIELD:
         if (c == delimiter) {
           if (sonicsv_unlikely((err = add_field(p, p->field_buffer, p->field_buffer_pos, true)) != CSV_OK)) break;
           p->state = CSV_STATE_FIELD_START;
           pos++;
         } else if (c == '\n') {
           if (sonicsv_unlikely((err = add_field(p, p->field_buffer, p->field_buffer_pos, true)) != CSV_OK)) break;
           if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
           p->state = CSV_STATE_FIELD_START;
           p->current_row_start_offset = original_bytes_processed + (pos + 1 - buf);
           pos++;
         } else if (c == '\r') {
           if (sonicsv_unlikely((err = add_field(p, p->field_buffer, p->field_buffer_pos, true)) != CSV_OK)) break;
           pos++;
           if (pos < end && *pos == '\n') pos++; // Skip LF in CRLF
           else if (pos == end && !is_final) p->pending_lf_skip = true;
           if (sonicsv_unlikely((err = finish_row(p)) != CSV_OK)) break;
           p->state = CSV_STATE_FIELD_START;
           p->current_row_start_offset = original_bytes_processed + (pos - buf);
         } else if (c == ' ' || c == '\t') {
           pos++;
         } else if (c == quote_char && p->options.double_quote) {
           // Escaped-quote pair `""` split across chunk boundary: the first
           // quote was consumed as a (provisional) closing quote at the end
           // of the previous chunk, leaving us in QUOTE_IN_QUOTED_FIELD with
           // the held quote in field_buffer. Now that we see the second one,
           // collapse them to a single literal quote and resume in-quoted.
           if (sonicsv_unlikely((err = append_to_field_buffer(p, &quote_char, 1)) != CSV_OK)) break;
           p->state = CSV_STATE_IN_QUOTED_FIELD;
           pos++;
         } else if (p->options.strict_mode) {
           err = report_error(p, CSV_ERROR_PARSE_ERROR, "Unexpected character after closing quote");
           break;
         } else {
           if (sonicsv_unlikely((err = append_to_field_buffer(p, &quote_char, 1)) != CSV_OK)) break;
           if (sonicsv_unlikely((err = append_to_field_buffer(p, &c, 1)) != CSV_OK)) break;
           p->state = CSV_STATE_IN_QUOTED_FIELD;
           pos++;
         }
         break;
     }
   }
 
   // FIXED: Handle final row when we reach end of input
   if (is_final && err == CSV_OK && p->num_fields > 0) {
     err = finish_row(p);
   }

   // Update statistics
   p->stats.total_bytes_processed = original_bytes_processed + (pos - buf);

   // Clear unparsed buffer if we consumed everything
   if (buf == p->unparsed_buffer && pos == end) {
     p->unparsed_size = 0;
   }

   // Stabilize fields when a chunk consumed all its bytes but a row is still
   // in progress (e.g., chunk ended exactly after a delimiter). The
   // partial-save branches above only fire when find() returns NULL on a
   // pending unquoted field; they don't catch the case where parsing
   // completed cleanly but no row terminator was hit. Without this, the
   // emitted fields' `data` pointers would dangle into a buffer that goes
   // out of scope (user's chunk) or gets overwritten on the next call
   // (unparsed_buffer).
   if (!is_final && err == CSV_OK && p->num_fields > 0) {
     csv_error_t serr = csv_stabilize_pending_fields(p, buf, end);
     if (serr != CSV_OK) return serr;
     if (buf != p->unparsed_buffer && p->unparsed_buffer) {
       serr = csv_stabilize_pending_fields(p, p->unparsed_buffer,
                                           p->unparsed_buffer + p->unparsed_capacity);
       if (serr != CSV_OK) return serr;
     }
   }

   return err;
 }
 
 // --- Public API Implementation ---
 csv_parse_options_t csv_default_options(void) {
     // No environment-variable lookups: a default-options helper should be
     // deterministic and side-effect-free. num_threads is reserved for
     // future parallel parsing (currently unused; enable_parallel = false).
     return (csv_parse_options_t){
         .delimiter = ',', .quote_char = '"', .double_quote = true,
         .trim_whitespace = false, .ignore_empty_lines = true, .strict_mode = false,
         .max_field_size = 10 * 1024 * 1024, .max_row_size = 100 * 1024 * 1024,
         .buffer_size = 64 * 1024, .max_memory_kb = 0, .current_memory = 0,
         .disable_mmap = false, .num_threads = 1, .enable_parallel = false,
         .enable_prefetch = true, .prefetch_distance = SONICSV_PREFETCH_DISTANCE,
         .force_alignment = true
     };
 }
 
 const char *csv_error_string(csv_error_t error) {
   switch (error) {
     case CSV_OK: return "Success";
     case CSV_ERROR_INVALID_ARGS: return "Invalid arguments";
     case CSV_ERROR_OUT_OF_MEMORY: return "Out of memory";
     case CSV_ERROR_PARSE_ERROR: return "Parse error";
     case CSV_ERROR_FIELD_TOO_LARGE: return "Field too large";
     case CSV_ERROR_ROW_TOO_LARGE: return "Row too large";
     case CSV_ERROR_IO_ERROR: return "I/O error";
     default: return "Unknown error";
   }
 }
 
 csv_parser_t *csv_parser_create(const csv_parse_options_t *options) {
   csv_parser_t *p = (csv_parser_t *)csv_aligned_alloc(sizeof(csv_parser_t), 64);
   if (!p) return NULL;
 
   memset(p, 0, sizeof(csv_parser_t));
   p->options = options ? *options : csv_default_options();
   p->state = CSV_STATE_FIELD_START;
   p->instance_id = atomic_fetch_add_explicit(&g_next_parser_id, 1, memory_order_relaxed);
  
  // Explicitly initialize all pointer fields to NULL
  p->unparsed_buffer = NULL;
  p->fields = NULL;
  p->field_buffer = NULL;
  p->field_data_pool = NULL;
  p->row_callback = NULL;
  p->row_callback_data = NULL;
  p->error_callback = NULL;
  p->error_callback_data = NULL;
  
  // Initialize all size fields to 0
  p->unparsed_size = 0;
  p->unparsed_capacity = 0;
  p->fields_capacity = 0;
  p->num_fields = 0;
  p->field_buffer_capacity = 0;
  p->field_buffer_pos = 0;
  p->field_data_pool_size = 0;
  p->field_data_pool_capacity = 0;
  p->peak_memory = 0;
  p->current_row_start_offset = 0;
  p->sum_field_size = 0;
  p->sum_row_size = 0;
  p->current_row_size = 0;
  p->simd_features = csv_get_simd_features();
  p->pending_lf_skip = false;

  // Initialize stats structure and start_time
  memset(&p->stats, 0, sizeof(csv_stats_t));
  memset(&p->start_time, 0, sizeof(struct timespec));

   if (ensure_capacity((void**)&p->fields, &p->fields_capacity, CSV_INITIAL_FIELD_CAPACITY, sizeof(csv_field_t), p) != CSV_OK) {
     csv_parser_destroy(p);
     return NULL;
   }

   csv_clock_now(&p->start_time);
   return p;
 }
 
 void csv_parser_destroy(csv_parser_t *p) {
   if (!p) return;
   csv_aligned_free(p->fields);
   csv_aligned_free(p->field_buffer);
   csv_aligned_free(p->unparsed_buffer);
   csv_aligned_free(p->field_data_pool);
   csv_aligned_free(p);
 }
 
 csv_error_t csv_parser_reset(csv_parser_t *p) {
   if (!p) return CSV_ERROR_INVALID_ARGS;
   p->state = CSV_STATE_FIELD_START;
   p->num_fields = 0;
   p->field_buffer_pos = 0;
   p->unparsed_size = 0;
   p->field_data_pool_size = 0;
   p->current_row_start_offset = 0;
   p->sum_field_size = 0;
   p->sum_row_size = 0;
   p->current_row_size = 0;
   p->simd_features = csv_get_simd_features();
   p->pending_lf_skip = false;
   // Keep SIMD cache initialized across resets
   memset(&p->stats, 0, sizeof(p->stats));
   csv_clock_now(&p->start_time);
   return CSV_OK;
 }
 
 void csv_parser_set_row_callback(csv_parser_t *p, csv_row_callback_t cb, void *ud) {
   if (!p) return;
   p->row_callback = cb;
   p->row_callback_data = ud;
 }
 
 void csv_parser_set_error_callback(csv_parser_t *p, csv_error_callback_t cb, void *ud) {
   if (!p) return;
   p->error_callback = cb;
   p->error_callback_data = ud;
 }
 
csv_error_t csv_parse_file(csv_parser_t *p, const char *filename) {
    // Enhanced parameter validation
    if (!p) return CSV_ERROR_INVALID_ARGS;
    if (!filename || strlen(filename) == 0) return CSV_ERROR_INVALID_ARGS;
    if (strlen(filename) > 4096) return CSV_ERROR_INVALID_ARGS;

#if !defined(_WIN32)
    // POSIX: zero-copy via mmap (unless disabled).
    if (!p->options.disable_mmap) {
        int fd = open(filename, O_RDONLY);
        if (fd < 0) return report_error(p, CSV_ERROR_IO_ERROR, "Could not open file");

        struct stat st;
        if (fstat(fd, &st) < 0) {
            close(fd);
            return report_error(p, CSV_ERROR_IO_ERROR, "Could not stat file");
        }

        // Reject non-regular files (directories, devices, sockets, FIFOs):
        // mmap on these has undefined or surprising behavior.
        if (!S_ISREG(st.st_mode)) {
            close(fd);
            return report_error(p, CSV_ERROR_IO_ERROR, "Not a regular file");
        }

        // st_size is off_t which can exceed size_t on a 32-bit address space.
        // Refuse to map files larger than the address space; the user can
        // fall back to streaming via csv_parse_stream.
        if (st.st_size < 0 || (uintmax_t)st.st_size > (uintmax_t)SIZE_MAX) {
            close(fd);
            return report_error(p, CSV_ERROR_IO_ERROR,
                                "File too large for in-memory parse");
        }

        size_t file_size = (size_t)st.st_size;
        if (file_size == 0) {
            close(fd);
            return CSV_OK; // Empty file
        }

        // Memory-map the file for zero-copy access
#ifdef __APPLE__
        void *mapped = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
#else
        void *mapped = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE | MAP_NORESERVE, fd, 0);
#endif
        close(fd);

        if (mapped == MAP_FAILED) {
            // Fallback to stream parsing
            FILE *f = fopen(filename, "rb");
            if (!f) return report_error(p, CSV_ERROR_IO_ERROR, "Could not open file");
            csv_error_t result = csv_parse_stream(p, f);
            fclose(f);
            return result;
        }

        // Advise the kernel about access pattern. POSIX_MADV_* / MADV_* are
        // *values*, not flags — bitwise OR'ing them is a silent semantic bug
        // (the OR coincidentally evaluates to WILLNEED). Issue two separate
        // calls instead. posix_madvise is the portable spelling; fall back
        // to madvise on systems where only it's available.
#if defined(__APPLE__) || defined(_POSIX_ADVISORY_INFO)
        posix_madvise(mapped, file_size, POSIX_MADV_SEQUENTIAL);
        posix_madvise(mapped, file_size, POSIX_MADV_WILLNEED);
#elif defined(MADV_SEQUENTIAL)
        madvise(mapped, file_size, MADV_SEQUENTIAL);
#  ifdef MADV_WILLNEED
        madvise(mapped, file_size, MADV_WILLNEED);
#  endif
#endif

        // Parse the entire file at once - zero copy
        csv_error_t result = csv_parse_buffer(p, (const char *)mapped, file_size, true);

        munmap(mapped, file_size);
        return result;
    }
#else
    // Windows: zero-copy via MapViewOfFile when not disabled.
    if (!p->options.disable_mmap) {
        HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER size;
            if (!GetFileSizeEx(hFile, &size)) {
                CloseHandle(hFile);
                return report_error(p, CSV_ERROR_IO_ERROR, "Could not stat file");
            }
            if (size.QuadPart == 0) {
                CloseHandle(hFile);
                return CSV_OK;
            }
            // Refuse to map files that exceed size_t (matters on 32-bit Windows).
            if ((unsigned long long)size.QuadPart > (unsigned long long)SIZE_MAX) {
                CloseHandle(hFile);
                return report_error(p, CSV_ERROR_IO_ERROR,
                                    "File too large for in-memory parse");
            }
            HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
            if (hMap) {
                void *mapped = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
                if (mapped) {
                    csv_error_t result = csv_parse_buffer(p, (const char*)mapped,
                                                          (size_t)size.QuadPart, true);
                    UnmapViewOfFile(mapped);
                    CloseHandle(hMap);
                    CloseHandle(hFile);
                    return result;
                }
                CloseHandle(hMap);
            }
            CloseHandle(hFile);
            // fall through to fopen-based stream parsing
        }
    }
#endif

    // Fallback: stream-based parsing
    FILE *f = fopen(filename, "rb");
    if (!f) return report_error(p, CSV_ERROR_IO_ERROR, "Could not open file");
    csv_error_t result = csv_parse_stream(p, f);
    fclose(f);
    return result;
}
 
 csv_error_t csv_parse_stream(csv_parser_t *p, FILE *stream) {
   // Enhanced parameter validation
   if (!p) return CSV_ERROR_INVALID_ARGS;
   if (!stream) return CSV_ERROR_INVALID_ARGS;
   if (ferror(stream)) return CSV_ERROR_IO_ERROR; // Check stream state
 
   char *buffer = (char *)csv_aligned_alloc(p->options.buffer_size, SONICSV_SIMD_ALIGN);
   if (!buffer) return CSV_ERROR_OUT_OF_MEMORY;
 
   csv_error_t err = CSV_OK;
   size_t bytes_read;
   while ((bytes_read = fread(buffer, 1, p->options.buffer_size, stream)) > 0) {
     if ((err = csv_parse_buffer(p, buffer, bytes_read, feof(stream))) != CSV_OK) break;
   }
 
   if (err == CSV_OK) err = csv_parse_buffer(p, "", 0, true);
   if (ferror(stream)) err = report_error(p, CSV_ERROR_IO_ERROR, "File read error");
   csv_aligned_free(buffer);
   return err;
 }
 
 csv_error_t csv_parse_string(csv_parser_t *p, const char *csv_line) {
     // Enhanced parameter validation
     if (!p) return CSV_ERROR_INVALID_ARGS;
     if (!csv_line) return CSV_ERROR_INVALID_ARGS;
     size_t len = strlen(csv_line);
     if (len > p->options.max_row_size) return CSV_ERROR_ROW_TOO_LARGE;
     return csv_parse_buffer(p, csv_line, len, true);
 }
 
 csv_stats_t csv_parser_get_stats(const csv_parser_t *p) {
   if (!p) return (csv_stats_t){0};
   csv_stats_t stats = p->stats;
   struct timespec current_time;
   csv_clock_now(&current_time);
   uint64_t elapsed_ns = (current_time.tv_sec - p->start_time.tv_sec) * 1000000000ULL +
                         (current_time.tv_nsec - p->start_time.tv_nsec);
   stats.parse_time_ns = elapsed_ns;
   if (elapsed_ns > 0) stats.throughput_mbps = (stats.total_bytes_processed / (1024.0 * 1024.0)) / (elapsed_ns / 1e9);
   // Explicit casts to uint32_t: stats fields are uint32_t but the MSVC
   // stdatomic shim returns `unsigned long long` and the memory arithmetic
   // is size_t. The values always fit, but /W3 flags the implicit narrowing.
   stats.simd_acceleration_used = (uint32_t)atomic_load_explicit(&g_simd_features_atomic, memory_order_acquire);
   stats.peak_memory_kb = (uint32_t)((p->fields_capacity * sizeof(csv_field_t) + p->field_buffer_capacity +
                          p->unparsed_capacity + p->field_data_pool_capacity) / 1024);

   // Derive averages from incremental sums (kept out of the per-field hot path).
   if (stats.total_fields_parsed > 0)
     stats.perf.avg_field_size = (double)p->sum_field_size / (double)stats.total_fields_parsed;
   if (stats.total_rows_parsed > 0)
     stats.perf.avg_row_size = (double)p->sum_row_size / (double)stats.total_rows_parsed;

   if (stats.peak_memory_kb > 0) {
     stats.perf.memory_efficiency = (double)stats.total_bytes_processed / (stats.peak_memory_kb * 1024);
   }

   return stats;
 }
 
 void csv_print_stats(const csv_parser_t *p) {
   if (!p) return;
   csv_stats_t s = csv_parser_get_stats(p);
   printf("--- SonicSV Parser Statistics ---\n"
          "  Bytes Processed: %llu\n  Rows Parsed:     %llu\n  Fields Parsed:   %llu\n"
          "  Parse Time:      %.3f ms\n  Throughput:      %.2f MB/s\n"
          "  Peak Memory:     %u KB\n  Errors:          %u\n"
          "  SIMD Operations: %llu\n  Scalar Fallbacks: %llu\n"
          "  SIMD Features:   ",
          (unsigned long long)s.total_bytes_processed, (unsigned long long)s.total_rows_parsed,
          (unsigned long long)s.total_fields_parsed, s.parse_time_ns / 1e6, s.throughput_mbps,
          s.peak_memory_kb, s.errors_encountered,
          (unsigned long long)s.perf.simd_ops, (unsigned long long)s.perf.scalar_fallbacks);
   if (s.simd_acceleration_used == CSV_SIMD_NONE) printf("None");
   else {
     if (s.simd_acceleration_used & CSV_SIMD_AVX512) printf("AVX-512 ");
     if (s.simd_acceleration_used & CSV_SIMD_AVX2) printf("AVX2 ");
     if (s.simd_acceleration_used & CSV_SIMD_SSE4_2) printf("SSE4.2 ");
     if (s.simd_acceleration_used & CSV_SIMD_NEON) printf("NEON ");
     if (s.simd_acceleration_used & CSV_SIMD_SVE) printf("SVE ");
   }
   printf("\n---------------------------------\n");
 }
 
uint32_t csv_get_simd_features(void) {
    uint32_t v = (uint32_t)atomic_load_explicit(&g_simd_features_atomic, memory_order_relaxed);
    if (sonicsv_likely(v != 0))
        return v & ~CSV_SIMD_INITIALIZED_FLAG;
    return csv_simd_init() & ~CSV_SIMD_INITIALIZED_FLAG;
}
 
 const csv_field_t *csv_get_field(const csv_row_t *row, size_t index) {
     if (!row || !row->fields || index >= row->num_fields) return NULL;
     return &row->fields[index];
 }
 
 size_t csv_get_num_fields(const csv_row_t *row) {
     if (!row) return 0;
     return row->num_fields;
 }
 
 // --- String Pool Implementation ---
 // Buckets store byte offsets into pool->data (not raw pointers). pool->data
 // is grown via ensure_capacity, which reallocates and frees the old buffer;
 // any cached `const char*` would dangle after that. Offsets stay valid
 // across resize. `used` distinguishes an empty slot from a real entry at
 // offset 0, since memset-zeroed buckets would otherwise look like valid
 // entries pointing at the start of pool->data.
 typedef struct {
   size_t offset;
   size_t length;
   uint32_t hash;
   uint32_t used;
 } csv_pool_entry_t;
 
 struct csv_string_pool {
   csv_pool_entry_t* buckets;
   uint32_t num_buckets;
   uint32_t num_items;
   char* data;
   size_t data_size;
   size_t data_capacity;
 } sonicsv_aligned(64);
 
 static sonicsv_always_inline uint32_t fnv1a_hash(const char* str, size_t len) {
   uint32_t hash = 0x811c9dc5;
   for (size_t i = 0; i < len; i++) {
     hash ^= (unsigned char)str[i];
     hash *= 0x01000193;
   }
   return hash;
 }
 
 static sonicsv_always_inline uint32_t next_power_of_2(uint32_t n) {
   if (n <= 1) return 2;
   n--;
   n |= n >> 1;
   n |= n >> 2;
   n |= n >> 4;
   n |= n >> 8;
   n |= n >> 16;
   return n + 1;
 }
 
 static csv_error_t pool_resize(csv_string_pool_t* pool) {
   uint32_t old_num_buckets = pool->num_buckets;
   csv_pool_entry_t* old_buckets = pool->buckets;
   uint32_t new_num_buckets = next_power_of_2(old_num_buckets * 2);
 
   csv_pool_entry_t* new_buckets = (csv_pool_entry_t*)csv_aligned_alloc(new_num_buckets * sizeof(csv_pool_entry_t), SONICSV_SIMD_ALIGN);
   if (!new_buckets) return CSV_ERROR_OUT_OF_MEMORY;
   memset(new_buckets, 0, new_num_buckets * sizeof(csv_pool_entry_t));
 
   for (uint32_t i = 0; i < old_num_buckets; i++) {
     if (old_buckets[i].used) {
       uint32_t index = old_buckets[i].hash & (new_num_buckets - 1);
       while (new_buckets[index].used) {
         index = (index + 1) & (new_num_buckets - 1);
       }
       new_buckets[index] = old_buckets[i];
     }
   }
 
   csv_aligned_free(old_buckets);
   pool->buckets = new_buckets;
   pool->num_buckets = new_num_buckets;
   return CSV_OK;
 }
 
 csv_string_pool_t *csv_string_pool_create(size_t initial_capacity) {
   csv_string_pool_t *pool = (csv_string_pool_t *)csv_aligned_alloc(sizeof(csv_string_pool_t), 64);
   if (!pool) return NULL;
 
   memset(pool, 0, sizeof(csv_string_pool_t));
   pool->data_capacity = initial_capacity > 0 ? initial_capacity : 4096;
   pool->data = (char *)csv_aligned_alloc(pool->data_capacity, SONICSV_SIMD_ALIGN);
   pool->num_buckets = 16;
   pool->buckets = (csv_pool_entry_t *)csv_aligned_alloc(pool->num_buckets * sizeof(csv_pool_entry_t), SONICSV_SIMD_ALIGN);
 
   if (!pool->data || !pool->buckets) {
     csv_string_pool_destroy(pool);
     return NULL;
   }
 
   memset(pool->buckets, 0, pool->num_buckets * sizeof(csv_pool_entry_t));
   return pool;
 }
 
 void csv_string_pool_destroy(csv_string_pool_t *pool) {
   if (!pool) return;
   csv_aligned_free(pool->buckets);
   csv_aligned_free(pool->data);
   csv_aligned_free(pool);
 }
 
 void csv_string_pool_clear(csv_string_pool_t *pool) {
   if (!pool) return;
   memset(pool->buckets, 0, pool->num_buckets * sizeof(csv_pool_entry_t));
   pool->num_items = 0;
   pool->data_size = 0;
 }
 
 const char *csv_string_pool_intern(csv_string_pool_t *pool, const char *str, size_t length) {
   if (!pool || !str) return NULL;
 
   if (pool->num_items * 4 >= pool->num_buckets * 3) {
     if (pool_resize(pool) != CSV_OK) return NULL;
   }
 
   uint32_t hash = fnv1a_hash(str, length);
   uint32_t index = hash & (pool->num_buckets - 1);

   while (pool->buckets[index].used) {
     if (pool->buckets[index].hash == hash &&
         pool->buckets[index].length == length &&
         memcmp(pool->data + pool->buckets[index].offset, str, length) == 0) {
       return pool->data + pool->buckets[index].offset;
     }
     index = (index + 1) & (pool->num_buckets - 1);
   }

   if (ensure_capacity((void**)&pool->data, &pool->data_capacity,
                      pool->data_size + length + 1, 1, NULL) != CSV_OK)
     return NULL;

   size_t new_offset = pool->data_size;
   char* new_str = pool->data + new_offset;
   memcpy(new_str, str, length);
   new_str[length] = '\0';
   pool->data_size += length + 1;

   pool->buckets[index] = (csv_pool_entry_t){new_offset, length, hash, 1};
   pool->num_items++;
   return new_str;
 }
 
 #endif // SONICSV_IMPLEMENTATION
 
 #endif // SONICSV_H
 
