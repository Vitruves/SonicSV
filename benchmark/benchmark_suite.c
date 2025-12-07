/*
 * SonicSV Benchmark Suite
 *
 * A comprehensive, fair comparison between SonicSV and libcsv parsers.
 * Generates test data, runs identical workloads, and produces detailed reports.
 *
 * Build: gcc -O3 -march=native -o benchmark_suite benchmark_suite.c -lcsv -lpthread -lm
 * Usage: ./benchmark_suite [--iterations N] [--warmup N] [--output FILE]
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

/* Include libcsv first to avoid conflicts */
#include <csv.h>

/* Now include SonicSV with renamed internal struct */
#define csv_parser sonicsv_parser_internal
/* SONICSV_IMPLEMENTATION is passed via Makefile -D flag */
#include "../sonicsv.h"
#undef csv_parser

/*
 * Configuration
 */
#define DEFAULT_ITERATIONS    5
#define DEFAULT_WARMUP        2
#define MAX_FIELD_SIZE        1024
#define MAX_FIELDS_PER_ROW    100
#define TEMP_DIR              "/tmp/sonicsv_bench"

/*
 * Test configurations
 */
typedef struct {
    const char *name;
    size_t rows;
    size_t fields_per_row;
    size_t avg_field_size;
    bool has_quotes;
    bool has_newlines_in_fields;
    bool has_commas_in_fields;
} test_config_t;

static const test_config_t test_configs[] = {
    /* Simple tests - no special characters */
    {"tiny_simple",      1000,     5,   10, false, false, false},
    {"small_simple",    10000,     5,   10, false, false, false},
    {"medium_simple",  100000,     5,   10, false, false, false},
    {"large_simple",   500000,     5,   10, false, false, false},

    /* Varying field counts */
    {"wide_10cols",    100000,    10,   10, false, false, false},
    {"wide_25cols",    100000,    25,   10, false, false, false},
    {"wide_50cols",    100000,    50,   10, false, false, false},

    /* Varying field sizes */
    {"long_fields",    100000,     5,   50, false, false, false},
    {"very_long",       50000,     5,  200, false, false, false},

    /* Complex tests - with quoted fields */
    {"quoted_simple",  100000,     5,   10, true,  false, false},
    {"quoted_commas",  100000,     5,   20, true,  false, true },
    {"quoted_newlines", 50000,     5,   30, true,  true,  false},
    {"quoted_mixed",    50000,     5,   30, true,  true,  true },
};

#define NUM_TESTS (sizeof(test_configs) / sizeof(test_configs[0]))

/*
 * Timing utilities
 */
typedef struct {
    double min;
    double max;
    double sum;
    double sum_sq;
    size_t count;
} timing_stats_t;

static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void stats_init(timing_stats_t *s) {
    s->min = 1e30;
    s->max = 0;
    s->sum = 0;
    s->sum_sq = 0;
    s->count = 0;
}

static void stats_add(timing_stats_t *s, double value) {
    if (value < s->min) s->min = value;
    if (value > s->max) s->max = value;
    s->sum += value;
    s->sum_sq += value * value;
    s->count++;
}

static double stats_mean(const timing_stats_t *s) {
    return s->count > 0 ? s->sum / s->count : 0;
}

static double stats_stddev(const timing_stats_t *s) {
    if (s->count < 2) return 0;
    double mean = stats_mean(s);
    double variance = (s->sum_sq / s->count) - (mean * mean);
    return variance > 0 ? sqrt(variance) : 0;
}

/*
 * Data generation
 */
static uint32_t g_rng_state = 12345;

static uint32_t rng_next(void) {
    g_rng_state = g_rng_state * 1103515245 + 12345;
    return (g_rng_state >> 16) & 0x7FFF;
}

static void rng_seed(uint32_t seed) {
    g_rng_state = seed;
}

static void generate_field(char *buf, size_t max_len, size_t target_len,
                           bool allow_comma, bool allow_newline) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
    size_t len = target_len + (rng_next() % (target_len / 2 + 1)) - target_len / 4;
    if (len < 1) len = 1;
    if (len >= max_len) len = max_len - 1;

    for (size_t i = 0; i < len; i++) {
        int r = rng_next() % 100;
        if (allow_comma && r < 3) {
            buf[i] = ',';
        } else if (allow_newline && r < 5) {
            buf[i] = '\n';
        } else {
            buf[i] = charset[rng_next() % (sizeof(charset) - 1)];
        }
    }
    buf[len] = '\0';
}

static size_t generate_test_file(const test_config_t *config, const char *filepath) {
    FILE *f = fopen(filepath, "wb");
    if (!f) {
        fprintf(stderr, "Error: Cannot create file %s: %s\n", filepath, strerror(errno));
        return 0;
    }

    rng_seed(42);  /* Deterministic for reproducibility */

    char field_buf[MAX_FIELD_SIZE];
    size_t total_bytes = 0;

    /* Generate header row */
    for (size_t col = 0; col < config->fields_per_row; col++) {
        if (col > 0) {
            fputc(',', f);
            total_bytes++;
        }
        int written = fprintf(f, "col%zu", col);
        total_bytes += written;
    }
    fputc('\n', f);
    total_bytes++;

    /* Generate data rows */
    for (size_t row = 0; row < config->rows; row++) {
        for (size_t col = 0; col < config->fields_per_row; col++) {
            if (col > 0) {
                fputc(',', f);
                total_bytes++;
            }

            generate_field(field_buf, MAX_FIELD_SIZE, config->avg_field_size,
                          config->has_commas_in_fields, config->has_newlines_in_fields);

            bool needs_quotes = config->has_quotes &&
                               (strchr(field_buf, ',') || strchr(field_buf, '\n') ||
                                strchr(field_buf, '"'));

            if (needs_quotes) {
                fputc('"', f);
                total_bytes++;
                for (char *p = field_buf; *p; p++) {
                    if (*p == '"') {
                        fputc('"', f);
                        fputc('"', f);
                        total_bytes += 2;
                    } else {
                        fputc(*p, f);
                        total_bytes++;
                    }
                }
                fputc('"', f);
                total_bytes++;
            } else {
                size_t len = strlen(field_buf);
                fwrite(field_buf, 1, len, f);
                total_bytes += len;
            }
        }
        fputc('\n', f);
        total_bytes++;
    }

    fclose(f);
    return total_bytes;
}

/*
 * Benchmark state - identical for both parsers
 */
typedef struct {
    uint64_t rows_parsed;
    uint64_t fields_parsed;
    uint64_t bytes_processed;
    volatile uint64_t checksum;  /* Prevent optimizer from removing work */
} bench_state_t;

/*
 * SonicSV callback
 */
static void sonicsv_row_callback(const csv_row_t *row, void *user_data) {
    bench_state_t *state = (bench_state_t *)user_data;
    state->rows_parsed++;
    state->fields_parsed += row->num_fields;

    /* Compute checksum to ensure we actually process data */
    for (size_t i = 0; i < row->num_fields; i++) {
        const csv_field_t *field = csv_get_field(row, i);
        if (field && field->data && field->size > 0) {
            state->checksum += (uint64_t)field->data[0];
        }
    }
}

/*
 * libcsv callbacks
 */
static void libcsv_field_callback(void *data, size_t len, void *user_data) {
    bench_state_t *state = (bench_state_t *)user_data;
    state->fields_parsed++;
    if (len > 0) {
        state->checksum += (uint64_t)((char *)data)[0];
    }
}

static void libcsv_row_callback(int delim, void *user_data) {
    (void)delim;
    bench_state_t *state = (bench_state_t *)user_data;
    state->rows_parsed++;
}

/*
 * Benchmark runners
 */
static double run_sonicsv_benchmark(const char *filepath, size_t file_size, bench_state_t *state) {
    memset(state, 0, sizeof(*state));

    csv_parser_t *parser = csv_parser_create(NULL);
    if (!parser) {
        fprintf(stderr, "Error: Failed to create SonicSV parser\n");
        return -1;
    }

    csv_parser_set_row_callback(parser, sonicsv_row_callback, state);

    uint64_t start = get_time_ns();
    csv_error_t result = csv_parse_file(parser, filepath);
    uint64_t end = get_time_ns();

    if (result != CSV_OK) {
        fprintf(stderr, "Error: SonicSV parse failed: %s\n", csv_error_string(result));
        csv_parser_destroy(parser);
        return -1;
    }

    state->bytes_processed = file_size;
    csv_parser_destroy(parser);

    return (double)(end - start) / 1e9;  /* Return seconds */
}

static double run_libcsv_benchmark(const char *filepath, size_t file_size, bench_state_t *state) {
    memset(state, 0, sizeof(*state));

    struct csv_parser parser;
    if (csv_init(&parser, CSV_STRICT) != 0) {
        fprintf(stderr, "Error: Failed to create libcsv parser\n");
        return -1;
    }

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file %s\n", filepath);
        csv_free(&parser);
        return -1;
    }

    char *buffer = malloc(65536);
    if (!buffer) {
        fclose(f);
        csv_free(&parser);
        return -1;
    }

    uint64_t start = get_time_ns();

    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, 65536, f)) > 0) {
        if (csv_parse(&parser, buffer, bytes_read,
                      libcsv_field_callback, libcsv_row_callback, state) != bytes_read) {
            fprintf(stderr, "Error: libcsv parse error: %s\n", csv_strerror(csv_error(&parser)));
            break;
        }
    }
    csv_fini(&parser, libcsv_field_callback, libcsv_row_callback, state);

    uint64_t end = get_time_ns();

    fclose(f);
    free(buffer);
    csv_free(&parser);

    state->bytes_processed = file_size;
    return (double)(end - start) / 1e9;
}

/*
 * Result storage
 */
typedef struct {
    const char *test_name;
    size_t file_size;
    size_t expected_rows;
    size_t expected_fields;

    timing_stats_t sonicsv_times;
    timing_stats_t libcsv_times;

    double sonicsv_throughput;
    double libcsv_throughput;
    double speedup;

    /* Validation data - row/field counts are more reliable than checksums */
    uint64_t sonicsv_rows;
    uint64_t sonicsv_fields;
    uint64_t libcsv_rows;
    uint64_t libcsv_fields;
} test_result_t;

/*
 * Report generation
 */
static void print_separator(FILE *out, int width) {
    for (int i = 0; i < width; i++) fputc('=', out);
    fputc('\n', out);
}

static void print_line(FILE *out, int width) {
    for (int i = 0; i < width; i++) fputc('-', out);
    fputc('\n', out);
}

static void print_report(FILE *out, test_result_t *results, size_t num_results,
                         int iterations, int warmup) {
    const int width = 95;

    fprintf(out, "\n");
    print_separator(out, width);
    fprintf(out, "SONICSV vs LIBCSV BENCHMARK REPORT\n");
    print_separator(out, width);

    /* System information */
    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

    fprintf(out, "\nTEST CONFIGURATION\n");
    print_line(out, width);
    fprintf(out, "  Timestamp:           %s\n", time_str);
    fprintf(out, "  Iterations:          %d (after %d warmup runs)\n", iterations, warmup);
    fprintf(out, "  Test cases:          %zu\n", num_results);

#ifdef __APPLE__
    fprintf(out, "  Platform:            macOS\n");
#else
    fprintf(out, "  Platform:            Linux\n");
#endif

#ifdef __aarch64__
    fprintf(out, "  Architecture:        ARM64 (NEON SIMD)\n");
#elif defined(__x86_64__)
    fprintf(out, "  Architecture:        x86_64 (SSE4.2/AVX2 SIMD)\n");
#else
    fprintf(out, "  Architecture:        Generic\n");
#endif

    /* Summary statistics */
    double total_sonicsv_time = 0, total_libcsv_time = 0;
    double total_bytes = 0;
    double min_speedup = 1e30, max_speedup = 0, sum_speedup = 0;
    int sonicsv_wins = 0, libcsv_wins = 0, ties = 0;

    for (size_t i = 0; i < num_results; i++) {
        total_sonicsv_time += stats_mean(&results[i].sonicsv_times);
        total_libcsv_time += stats_mean(&results[i].libcsv_times);
        total_bytes += results[i].file_size;
        sum_speedup += results[i].speedup;
        if (results[i].speedup < min_speedup) min_speedup = results[i].speedup;
        if (results[i].speedup > max_speedup) max_speedup = results[i].speedup;
        if (results[i].speedup > 1.05) sonicsv_wins++;
        else if (results[i].speedup < 0.95) libcsv_wins++;
        else ties++;
    }

    double avg_sonicsv_throughput = (total_bytes / (1024.0 * 1024.0)) / total_sonicsv_time;
    double avg_libcsv_throughput = (total_bytes / (1024.0 * 1024.0)) / total_libcsv_time;

    fprintf(out, "\nOVERALL SUMMARY\n");
    print_line(out, width);
    fprintf(out, "  SonicSV victories:   %d / %zu tests (>5%% faster)\n", sonicsv_wins, num_results);
    fprintf(out, "  libcsv victories:    %d / %zu tests (>5%% faster)\n", libcsv_wins, num_results);
    fprintf(out, "  Ties:                %d / %zu tests (within 5%%)\n", ties, num_results);
    fprintf(out, "\n");
    fprintf(out, "  Aggregate SonicSV:   %.1f MB/s (total: %.2f MB in %.3f s)\n",
            avg_sonicsv_throughput, total_bytes / (1024.0 * 1024.0), total_sonicsv_time);
    fprintf(out, "  Aggregate libcsv:    %.1f MB/s (total: %.2f MB in %.3f s)\n",
            avg_libcsv_throughput, total_bytes / (1024.0 * 1024.0), total_libcsv_time);
    fprintf(out, "  Aggregate speedup:   %.2fx\n", avg_sonicsv_throughput / avg_libcsv_throughput);
    fprintf(out, "\n");
    fprintf(out, "  Per-test speedup:\n");
    fprintf(out, "    Average:           %.2fx\n", sum_speedup / num_results);
    fprintf(out, "    Minimum:           %.2fx\n", min_speedup);
    fprintf(out, "    Maximum:           %.2fx\n", max_speedup);

    /* Detailed results table */
    fprintf(out, "\nDETAILED RESULTS BY TEST\n");
    print_separator(out, width);

    fprintf(out, "\n%-18s %8s %10s %10s %8s %8s %6s\n",
            "Test", "Size", "SonicSV", "libcsv", "Speedup", "Winner", "Valid");
    fprintf(out, "%-18s %8s %10s %10s %8s %8s %6s\n",
            "", "(MB)", "(MB/s)", "(MB/s)", "", "", "");
    print_line(out, width);

    for (size_t i = 0; i < num_results; i++) {
        test_result_t *r = &results[i];
        double size_mb = r->file_size / (1024.0 * 1024.0);
        const char *winner = r->speedup > 1.05 ? "SonicSV" :
                            (r->speedup < 0.95 ? "libcsv" : "-");
        /* Validate based on row counts - more reliable than byte-level checksums */
        bool counts_match = (r->sonicsv_rows == r->libcsv_rows) &&
                           (r->sonicsv_fields == r->libcsv_fields);
        const char *valid = counts_match ? "yes" : "FAIL";

        fprintf(out, "%-18s %8.2f %10.1f %10.1f %7.2fx %8s %6s\n",
                r->test_name, size_mb, r->sonicsv_throughput, r->libcsv_throughput,
                r->speedup, winner, valid);
    }

    print_line(out, width);

    /* Timing variance details */
    fprintf(out, "\nTIMING VARIANCE (seconds, lower is better)\n");
    print_line(out, width);

    fprintf(out, "\n%-18s %10s %10s %10s %10s\n",
            "Test", "SonicSV", "(stddev)", "libcsv", "(stddev)");
    print_line(out, width);

    for (size_t i = 0; i < num_results; i++) {
        test_result_t *r = &results[i];
        fprintf(out, "%-18s %10.4f %10.4f %10.4f %10.4f\n",
                r->test_name,
                stats_mean(&r->sonicsv_times),
                stats_stddev(&r->sonicsv_times),
                stats_mean(&r->libcsv_times),
                stats_stddev(&r->libcsv_times));
    }

    print_separator(out, width);
    fprintf(out, "End of benchmark report.\n\n");
}

/*
 * Main benchmark runner
 */
static int run_benchmark_suite(int iterations, int warmup, FILE *report_out) {
    test_result_t results[NUM_TESTS];
    memset(results, 0, sizeof(results));

    /* Create temp directory */
    mkdir(TEMP_DIR, 0755);

    fprintf(stderr, "\nSonicSV Benchmark Suite\n");
    fprintf(stderr, "=======================\n\n");
    fprintf(stderr, "Configuration: %zu tests, %d iterations, %d warmup\n\n",
            NUM_TESTS, iterations, warmup);

    fprintf(stderr, "%-4s %-18s %8s %10s %10s %8s\n",
            "#", "Test", "Size", "SonicSV", "libcsv", "Speedup");
    fprintf(stderr, "---- ------------------ -------- ---------- ---------- --------\n");

    for (size_t t = 0; t < NUM_TESTS; t++) {
        const test_config_t *config = &test_configs[t];
        test_result_t *result = &results[t];

        result->test_name = config->name;
        stats_init(&result->sonicsv_times);
        stats_init(&result->libcsv_times);

        /* Generate test file */
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "%s/%s.csv", TEMP_DIR, config->name);

        size_t file_size = generate_test_file(config, filepath);
        if (file_size == 0) {
            fprintf(stderr, "[%2zu] %-18s FAILED (data generation)\n", t + 1, config->name);
            continue;
        }

        result->file_size = file_size;
        result->expected_rows = config->rows + 1;  /* +1 for header row */
        result->expected_fields = (config->rows + 1) * config->fields_per_row;

        bench_state_t state;

        /* Warmup runs */
        for (int w = 0; w < warmup; w++) {
            run_sonicsv_benchmark(filepath, file_size, &state);
            run_libcsv_benchmark(filepath, file_size, &state);
        }

        /* Timed runs - SonicSV */
        for (int i = 0; i < iterations; i++) {
            double elapsed = run_sonicsv_benchmark(filepath, file_size, &state);
            if (elapsed > 0) {
                stats_add(&result->sonicsv_times, elapsed);
            }
            if (i == iterations - 1) {
                result->sonicsv_rows = state.rows_parsed;
                result->sonicsv_fields = state.fields_parsed;
            }
        }

        /* Timed runs - libcsv */
        for (int i = 0; i < iterations; i++) {
            double elapsed = run_libcsv_benchmark(filepath, file_size, &state);
            if (elapsed > 0) {
                stats_add(&result->libcsv_times, elapsed);
            }
            if (i == iterations - 1) {
                result->libcsv_rows = state.rows_parsed;
                result->libcsv_fields = state.fields_parsed;
            }
        }

        /* Calculate throughput */
        double sonicsv_mean = stats_mean(&result->sonicsv_times);
        double libcsv_mean = stats_mean(&result->libcsv_times);

        result->sonicsv_throughput = (file_size / (1024.0 * 1024.0)) / sonicsv_mean;
        result->libcsv_throughput = (file_size / (1024.0 * 1024.0)) / libcsv_mean;
        result->speedup = result->sonicsv_throughput / result->libcsv_throughput;

        fprintf(stderr, "[%2zu] %-18s %6.1fMB %8.1fMB/s %8.1fMB/s %6.2fx\n",
                t + 1, config->name,
                file_size / (1024.0 * 1024.0),
                result->sonicsv_throughput,
                result->libcsv_throughput,
                result->speedup);

        /* Clean up test file */
        unlink(filepath);
    }

    fprintf(stderr, "\nGenerating report...\n");

    /* Generate report */
    print_report(report_out, results, NUM_TESTS, iterations, warmup);

    /* Cleanup */
    rmdir(TEMP_DIR);

    return 0;
}

/*
 * Entry point
 */
int main(int argc, char **argv) {
    int iterations = DEFAULT_ITERATIONS;
    int warmup = DEFAULT_WARMUP;
    const char *output_file = NULL;

    static struct option long_options[] = {
        {"iterations", required_argument, 0, 'i'},
        {"warmup",     required_argument, 0, 'w'},
        {"output",     required_argument, 0, 'o'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "i:w:o:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'i':
                iterations = atoi(optarg);
                if (iterations < 1) iterations = 1;
                break;
            case 'w':
                warmup = atoi(optarg);
                if (warmup < 0) warmup = 0;
                break;
            case 'o':
                output_file = optarg;
                break;
            case 'h':
            default:
                fprintf(stderr, "SonicSV Benchmark Suite\n\n");
                fprintf(stderr, "Usage: %s [options]\n\n", argv[0]);
                fprintf(stderr, "Options:\n");
                fprintf(stderr, "  -i, --iterations N   Timed iterations per test (default: %d)\n", DEFAULT_ITERATIONS);
                fprintf(stderr, "  -w, --warmup N       Warmup iterations per test (default: %d)\n", DEFAULT_WARMUP);
                fprintf(stderr, "  -o, --output FILE    Write report to file (default: stdout)\n");
                fprintf(stderr, "  -h, --help           Show this help message\n\n");
                fprintf(stderr, "This tool generates CSV test data, parses it with both SonicSV and\n");
                fprintf(stderr, "libcsv under identical conditions, and produces a detailed comparison.\n");
                return opt == 'h' ? 0 : 1;
        }
    }

    FILE *report_out = stdout;
    if (output_file) {
        report_out = fopen(output_file, "w");
        if (!report_out) {
            fprintf(stderr, "Error: Cannot open output file %s: %s\n", output_file, strerror(errno));
            return 1;
        }
    }

    int result = run_benchmark_suite(iterations, warmup, report_out);

    if (output_file) {
        fclose(report_out);
        fprintf(stderr, "Report written to: %s\n", output_file);
    }

    return result;
}
