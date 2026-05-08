# SonicSV

<p align="center">
  <img src="img/sonicsv.png" alt="SonicSV" width="320">
</p>

<p align="center">
  <a href="https://github.com/vitruves/SonicSV"><img src="https://img.shields.io/badge/version-3.2.0-blue.svg" alt="Version"></a>
  <a href="https://github.com/vitruves/SonicSV/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-green.svg" alt="License"></a>
  <a href="https://en.wikipedia.org/wiki/C99"><img src="https://img.shields.io/badge/standard-C99-blue.svg" alt="C99"></a>
  <a href="https://github.com/vitruves/SonicSV"><img src="https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg" alt="Platform"></a>
</p>

A fast, single-header CSV parser for C with automatic SIMD acceleration (SSE4.2, AVX2, AVX-512, NEON, SVE).

## Features

- Single header, zero dependencies — drop `sonicsv.h` into your project
- Runtime SIMD dispatch with optimized scalar fallback
- Streaming callback API; parses files larger than RAM
- RFC 4180 compliant (quoted fields, escaped quotes, CRLF)
- Configurable limits, custom delimiters, thread-safe parser instances

## Quick Start

```c
#define SONICSV_IMPLEMENTATION
#include "sonicsv.h"

void on_row(const csv_row_t *row, void *ctx) {
    for (size_t i = 0; i < row->num_fields; i++) {
        const csv_field_t *f = csv_get_field(row, i);
        printf("%.*s ", (int)f->size, f->data);
    }
    printf("\n");
}

int main(void) {
    csv_parser_t *p = csv_parser_create(NULL);
    csv_parser_set_row_callback(p, on_row, NULL);
    csv_parse_file(p, "data.csv");
    csv_parser_destroy(p);
}
```

Compile:

```bash
# GCC / Clang / MinGW
gcc -O3 -march=native your_program.c -o your_program

# MSVC (native Windows)
cl /std:c11 /O2 your_program.c
```

### Build-time options

Both options are preprocessor defines that must appear **before** `#include "sonicsv.h"`. They behave identically whether set in code or on the command line — the table just lists the conventional way to set each.

**`SONICSV_IMPLEMENTATION`** — required, set in code

In **exactly one** `.c` file, `#define` it before the include to emit the implementation. Other TUs just `#include "sonicsv.h"` normally.

```c
#define SONICSV_IMPLEMENTATION
#include "sonicsv.h"
```

**`SONICSV_HAVE_STDATOMIC`** — optional, MSVC only, set on the command line

By default on MSVC, sonicsv uses a bundled `_Interlocked*`-intrinsics shim, so it builds out of the box on any MSVC version. Define this to use real `<stdatomic.h>` instead — requires MSVC 17.8+ with `/std:c11 /experimental:c11atomics`. GCC, Clang, MinGW, and clang-cl always use `<stdatomic.h>`; this flag is a no-op for them.

```bash
cl /std:c11 /experimental:c11atomics /DSONICSV_HAVE_STDATOMIC /O2 your_program.c
```

CPU SIMD is auto-detected at runtime (per-function `__attribute__((target(...)))` on GCC/Clang; scalar fallback on MSVC). `-march=native` is fine for local builds; for portable binaries just use `-O3` — the dispatcher still picks AVX-512 / AVX2 / SSE4.2 / NEON when the host CPU supports them.

### Using from C++

The public API is `extern "C"`, so you can call it directly from C++. Put the implementation in a single `.c` TU and let your `.cpp` files include only the public header:

```c
// sonicsv_impl.c — compile as C
#define SONICSV_IMPLEMENTATION
#include "sonicsv.h"
```

```cpp
// app.cpp — compile as C++, link against sonicsv_impl.o
#include "sonicsv.h"
// ... call csv_parser_create, csv_parse_file, etc.
```

Don't put `SONICSV_IMPLEMENTATION` in a `.cpp` file — the impl uses C-isms (`void*` implicit conversions, `<stdatomic.h>`) that don't compile cleanly as C++.

## Performance

MacBook Air M3 (NEON), 10 iterations vs libcsv. Selected results from `./build/benchmark_suite`:

| Test Case          |   Size | SonicSV    | libcsv    | Speedup |
| ------------------ | -----: | ---------- | --------- | ------: |
| small_simple       |  0.5MB | 1836 MB/s  | 222 MB/s  |   8.3x  |
| medium_simple      |  5.5MB | 2808 MB/s  | 324 MB/s  |   8.7x  |
| wide_25cols        | 27.4MB | 2777 MB/s  | 316 MB/s  |   8.8x  |
| long_fields        | 24.6MB | 4807 MB/s  | 343 MB/s  |  14.0x  |
| very_long          | 47.9MB | 6687 MB/s  | 350 MB/s  |  19.1x  |
| quoted_commas      | 10.4MB | 1932 MB/s  | 320 MB/s  |   6.1x  |
| quoted_mixed       |  7.9MB | 2874 MB/s  | 339 MB/s  |   8.5x  |
| huge_simple        |  110MB | 2620 MB/s  | 292 MB/s  |   9.0x  |
| huge_long          |  240MB | 6294 MB/s  | 337 MB/s  |  18.7x  |
| huge_quoted_mix    |   79MB | 2807 MB/s  | 332 MB/s  |   8.5x  |

Typical speedup **8–9x** on simple/quoted CSV, up to **19x** on long fields.

## Installation

```bash
curl -O https://raw.githubusercontent.com/vitruves/SonicSV/main/sonicsv.h
```

Or system-wide:

```bash
git clone https://github.com/vitruves/SonicSV.git && cd SonicSV
make install              # /usr/local/include
make install PREFIX=...   # custom prefix
```

Build targets: `make test`, `make example`, `make benchmark`, `make clean`.

## API

```c
// Lifecycle
csv_parser_t *csv_parser_create(const csv_parse_options_t *options);
csv_error_t   csv_parser_reset(csv_parser_t *parser);
void          csv_parser_destroy(csv_parser_t *parser);

// Parse
csv_error_t csv_parse_file  (csv_parser_t *p, const char *filename);
csv_error_t csv_parse_stream(csv_parser_t *p, FILE *stream);
csv_error_t csv_parse_string(csv_parser_t *p, const char *s);
csv_error_t csv_parse_buffer(csv_parser_t *p, const char *buf, size_t n, bool is_final);

// Callbacks
void csv_parser_set_row_callback  (csv_parser_t *p, csv_row_callback_t cb,   void *user_data);
void csv_parser_set_error_callback(csv_parser_t *p, csv_error_callback_t cb, void *user_data);

// Access
const csv_field_t *csv_get_field(const csv_row_t *row, size_t index);
size_t             csv_get_num_fields(const csv_row_t *row);

// Diagnostics
csv_stats_t csv_parser_get_stats(const csv_parser_t *parser);
void        csv_print_stats(const csv_parser_t *parser);
uint32_t    csv_get_simd_features(void);
const char *csv_error_string(csv_error_t error);
```

### Options

```c
csv_parse_options_t opts = csv_default_options();
opts.delimiter        = '\t';
opts.quote_char       = '"';
opts.trim_whitespace  = true;
opts.strict_mode      = true;
opts.max_field_size   = 1 * 1024 * 1024;   // 1 MB
opts.max_row_size     = 10 * 1024 * 1024;  // 10 MB
opts.buffer_size      = 128 * 1024;        // 128 KB
opts.max_memory_kb    = 0;                 // 0 = unlimited
```

### Field & Row

```c
typedef struct { const char *data; size_t size; bool quoted; } csv_field_t;
typedef struct { csv_field_t *fields; size_t num_fields;
                 uint64_t row_number; size_t byte_offset; } csv_row_t;
```

Field `data` is **not** null-terminated; always use `size`.

## Advanced Usage

The collapsible sections below cover edge features that most users won't need, but which are useful for streaming pipelines, long-running processes, memory-bounded environments, and diagnostics.

<details>
<summary><b>Streaming and chunked input (<code>csv_parse_buffer</code>)</b></summary>

Use `csv_parse_buffer` when data arrives in chunks (sockets, pipes, decompression, custom I/O). The parser holds incomplete rows across calls; pass `is_final = true` on the last chunk so any trailing row without a newline is flushed.

```c
csv_parser_t *p = csv_parser_create(NULL);
csv_parser_set_row_callback(p, on_row, NULL);

char buf[64 * 1024];
size_t n;
while ((n = read_chunk(buf, sizeof buf)) > 0) {
    if (csv_parse_buffer(p, buf, n, false) != CSV_OK) break;
}
csv_parse_buffer(p, NULL, 0, true);  // flush final partial row
csv_parser_destroy(p);
```

`csv_parse_string` is a convenience wrapper around this for null-terminated input. `csv_parse_stream` wraps a `FILE*` and `csv_parse_file` mmaps the file when possible (fall back to stream I/O via `opts.disable_mmap = true`).

</details>

<details>
<summary><b>Reusing a parser across files (<code>csv_parser_reset</code>)</b></summary>

`csv_parser_reset` clears parser state (row counter, statistics, internal buffers) without freeing pool allocations, so the next parse avoids reallocation. Useful in batch jobs and long-running services.

```c
csv_parser_t *p = csv_parser_create(NULL);
csv_parser_set_row_callback(p, on_row, NULL);

for (int i = 0; i < num_files; i++) {
    csv_parser_reset(p);
    csv_parse_file(p, files[i]);
}
csv_parser_destroy(p);
```

Callbacks survive a reset; options do not need to be reapplied.

</details>

<details>
<summary><b>Error callback and strict mode</b></summary>

By default the parser is lenient (RFC 4180 deviations are tolerated). Set `strict_mode = true` to surface malformed input as `CSV_ERROR_PARSE_ERROR`. Wire an error callback for per-row diagnostics — it receives the row number where the error occurred.

```c
void on_error(csv_error_t err, const char *msg, uint64_t row, void *ctx) {
    fprintf(stderr, "row %llu: %s (%s)\n",
            (unsigned long long)row, msg, csv_error_string(err));
}

csv_parse_options_t opts = csv_default_options();
opts.strict_mode = true;

csv_parser_t *p = csv_parser_create(&opts);
csv_parser_set_error_callback(p, on_error, NULL);
```

Parse functions also return a `csv_error_t`; the callback is for granular reporting, the return value is for control flow.

</details>

<details>
<summary><b>String pool (deduplicating repeated values)</b></summary>

When a column has low cardinality (categorical data, enum-like strings, repeated identifiers), interning saves memory and enables pointer-equality comparisons. The pool is independent of the parser; copy field bytes into it as you process rows.

```c
csv_string_pool_t *pool = csv_string_pool_create(1024);

void on_row(const csv_row_t *row, void *ctx) {
    csv_string_pool_t *p = ctx;
    const csv_field_t *f = csv_get_field(row, 0);
    const char *interned = csv_string_pool_intern(p, f->data, f->size);
    // 'interned' is null-terminated and stable for the pool's lifetime
}

csv_parser_set_row_callback(parser, on_row, pool);
// ... parse ...
csv_string_pool_clear(pool);    // reuse without realloc
csv_string_pool_destroy(pool);
```

Field `data` pointers from the row callback are **not** stable past the callback's return — always copy or intern values you need to keep.

</details>

<details>
<summary><b>Memory limits and large-field protection</b></summary>

For untrusted input, cap memory and field/row sizes to prevent a malformed file (e.g. a single unterminated quote) from exhausting RAM. Hitting a limit returns `CSV_ERROR_FIELD_TOO_LARGE`, `CSV_ERROR_ROW_TOO_LARGE`, or `CSV_ERROR_OUT_OF_MEMORY`.

```c
csv_parse_options_t opts = csv_default_options();
opts.max_field_size = 64 * 1024;        // 64 KB per field
opts.max_row_size   = 1 * 1024 * 1024;  // 1 MB per row
opts.max_memory_kb  = 256 * 1024;       // 256 MB total
```

`max_memory_kb = 0` (the default) means unlimited. Defaults for field/row sizes are 10 MB / 100 MB respectively — generous, but a determined attacker can still allocate a lot before hitting them.

</details>

<details>
<summary><b>Runtime SIMD detection</b></summary>

The parser auto-dispatches; you don't need to query SIMD features for correctness. `csv_get_simd_features` is a diagnostic — useful for logging which path a deployed binary actually took.

```c
uint32_t f = csv_get_simd_features();
printf("SIMD:%s%s%s%s%s\n",
    f & CSV_SIMD_AVX512 ? " AVX-512" : "",
    f & CSV_SIMD_AVX2   ? " AVX2"    : "",
    f & CSV_SIMD_SSE4_2 ? " SSE4.2"  : "",
    f & CSV_SIMD_NEON   ? " NEON"    : "",
    f & CSV_SIMD_SVE    ? " SVE"     : "");
```

Flags: `CSV_SIMD_NONE`, `CSV_SIMD_SSE4_2`, `CSV_SIMD_AVX2`, `CSV_SIMD_AVX512`, `CSV_SIMD_NEON`, `CSV_SIMD_SVE`. The result is a bitmask — multiple flags can be set on the same CPU.

</details>

<details>
<summary><b>Statistics and throughput measurement</b></summary>

`csv_parser_get_stats` returns a struct with totals, throughput (MB/s), peak memory, SIMD-vs-scalar op counts, and average field/row sizes. `csv_print_stats` dumps the same to stdout — handy for benchmarking and regression checks.

```c
csv_parse_file(p, "data.csv");

csv_stats_t s = csv_parser_get_stats(p);
printf("%llu rows, %.1f MB/s, peak %u KB, %llu SIMD / %llu scalar\n",
       (unsigned long long)s.total_rows_parsed,
       s.throughput_mbps,
       s.peak_memory_kb,
       (unsigned long long)s.perf.simd_ops,
       (unsigned long long)s.perf.scalar_fallbacks);
```

Stats accumulate across `csv_parse_*` calls on the same parser; `csv_parser_reset` zeroes them.

</details>

<details>
<summary><b>Tuning options for unusual workloads</b></summary>

`csv_default_options()` is tuned for general-purpose workloads. Adjust when you have a specific shape:

| Option | When to change |
| --- | --- |
| `delimiter`, `quote_char` | TSV (`'\t'`), pipe-delimited, alternative quoting |
| `double_quote = false` | Backslash-escape dialects (non-RFC) |
| `trim_whitespace = true` | Tolerate sloppy producers; costs a small per-field pass |
| `ignore_empty_lines = false` | Preserve blank rows (e.g. fixed schemas with optional sections) |
| `buffer_size` | Raise (1–4 MB) for huge files; lower for low-latency streaming |
| `disable_mmap = true` | Special filesystems, FUSE, or when you need stream semantics |
| `enable_prefetch = false` | Tiny files where prefetch overhead exceeds benefit |
| `force_alignment = false` | Embedded targets where aligned alloc is expensive |

`num_threads` and `enable_parallel` are reserved for future parallel parsing — currently no-ops.

</details>

## Thread Safety

Each `csv_parser_t` is independent and safe to use concurrently across threads. Do not share a single instance without external synchronization.

## Platforms

ARM64 (NEON), x86_64 (SSE4.2/AVX2/AVX-512, runtime-detected), portable scalar fallback elsewhere. Tested on Linux, macOS, Windows (MSVC, MinGW).

## License

MIT — see `sonicsv.h`.
