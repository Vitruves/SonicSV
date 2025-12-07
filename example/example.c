/*
 * SonicSV Example - CSV Data Processing
 *
 * Demonstrates common use cases:
 *   1. Basic file parsing with row callback
 *   2. Extracting specific columns by header name
 *   3. Aggregating numeric data
 *   4. Custom delimiter parsing
 *   5. Error handling
 *
 * Build: gcc -std=c99 -O3 -march=native -DSONICSV_IMPLEMENTATION \
 *        -o example example.c -lpthread -lm
 *
 * Run:   ./example                    # Uses sample data
 *        ./example <file.csv>         # Parse your own file
 */

/* SONICSV_IMPLEMENTATION is passed via Makefile -D flag */
#include "../sonicsv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================================
 * Example 1: Simple Row Printing
 * ============================================================================ */

static void print_row_callback(const csv_row_t *row, void *user_data) {
    int *row_count = (int *)user_data;
    (*row_count)++;

    printf("Row %d: ", *row_count);
    for (size_t i = 0; i < row->num_fields; i++) {
        const csv_field_t *field = csv_get_field(row, i);
        if (i > 0) printf(", ");
        printf("'%.*s'", (int)field->size, field->data);
    }
    printf("\n");
}

static void example_simple_parsing(void) {
    printf("=== Example 1: Simple Row Printing ===\n\n");

    const char *csv_data =
        "name,age,city\n"
        "Alice,28,Seattle\n"
        "Bob,35,Portland\n"
        "Carol,42,San Francisco\n";

    csv_parser_t *parser = csv_parser_create(NULL);
    int row_count = 0;

    csv_parser_set_row_callback(parser, print_row_callback, &row_count);
    csv_parse_string(parser, csv_data);
    csv_parser_destroy(parser);

    printf("\nTotal rows: %d\n\n", row_count);
}

/* ============================================================================
 * Example 2: Column Extraction by Header Name
 * ============================================================================ */

typedef struct {
    int name_col;
    int price_col;
    int quantity_col;
    int header_parsed;
    double total_value;
    int item_count;
} inventory_ctx_t;

static int find_column(const csv_row_t *row, const char *name) {
    for (size_t i = 0; i < row->num_fields; i++) {
        const csv_field_t *f = csv_get_field(row, i);
        if (f->size == strlen(name) && memcmp(f->data, name, f->size) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static double field_to_double(const csv_field_t *field) {
    char buf[64];
    size_t len = field->size < 63 ? field->size : 63;
    memcpy(buf, field->data, len);
    buf[len] = '\0';
    return atof(buf);
}

static void inventory_callback(const csv_row_t *row, void *user_data) {
    inventory_ctx_t *ctx = (inventory_ctx_t *)user_data;

    if (!ctx->header_parsed) {
        ctx->name_col = find_column(row, "product");
        ctx->price_col = find_column(row, "price");
        ctx->quantity_col = find_column(row, "quantity");
        ctx->header_parsed = 1;

        if (ctx->name_col < 0 || ctx->price_col < 0 || ctx->quantity_col < 0) {
            fprintf(stderr, "Error: Missing required columns\n");
        }
        return;
    }

    if (ctx->name_col < 0) return;

    const csv_field_t *name = csv_get_field(row, ctx->name_col);
    double price = field_to_double(csv_get_field(row, ctx->price_col));
    double quantity = field_to_double(csv_get_field(row, ctx->quantity_col));
    double value = price * quantity;

    printf("  %-15.*s  $%8.2f x %3.0f = $%10.2f\n",
           (int)name->size, name->data, price, quantity, value);

    ctx->total_value += value;
    ctx->item_count++;
}

static void example_column_extraction(void) {
    printf("=== Example 2: Inventory Value Calculation ===\n\n");

    const char *csv_data =
        "product,price,quantity,category\n"
        "Laptop,999.99,15,Electronics\n"
        "Mouse,29.99,150,Electronics\n"
        "Keyboard,79.99,85,Electronics\n"
        "Desk,299.99,20,Furniture\n"
        "Chair,199.99,45,Furniture\n";

    csv_parser_t *parser = csv_parser_create(NULL);
    inventory_ctx_t ctx = {0};

    csv_parser_set_row_callback(parser, inventory_callback, &ctx);
    csv_parse_string(parser, csv_data);
    csv_parser_destroy(parser);

    printf("  %-15s  %8s   %3s   %10s\n", "", "", "", "----------");
    printf("  %-15s  %8s   %3s   $%10.2f\n", "TOTAL", "", "", ctx.total_value);
    printf("\n  Items processed: %d\n\n", ctx.item_count);
}

/* ============================================================================
 * Example 3: Statistics Aggregation
 * ============================================================================ */

typedef struct {
    int value_col;
    int header_parsed;
    double sum;
    double min;
    double max;
    int count;
} stats_ctx_t;

static void stats_callback(const csv_row_t *row, void *user_data) {
    stats_ctx_t *ctx = (stats_ctx_t *)user_data;

    if (!ctx->header_parsed) {
        ctx->value_col = find_column(row, "value");
        ctx->header_parsed = 1;
        ctx->min = 1e30;
        ctx->max = -1e30;
        return;
    }

    if (ctx->value_col < 0) return;

    double value = field_to_double(csv_get_field(row, ctx->value_col));
    ctx->sum += value;
    ctx->count++;
    if (value < ctx->min) ctx->min = value;
    if (value > ctx->max) ctx->max = value;
}

static void example_statistics(void) {
    printf("=== Example 3: Statistical Aggregation ===\n\n");

    const char *csv_data =
        "id,value,label\n"
        "1,42.5,A\n"
        "2,18.3,B\n"
        "3,95.7,A\n"
        "4,33.2,C\n"
        "5,67.8,B\n"
        "6,51.4,A\n"
        "7,29.1,C\n"
        "8,88.6,B\n";

    csv_parser_t *parser = csv_parser_create(NULL);
    stats_ctx_t ctx = {0};

    csv_parser_set_row_callback(parser, stats_callback, &ctx);
    csv_parse_string(parser, csv_data);
    csv_parser_destroy(parser);

    printf("  Count:   %d\n", ctx.count);
    printf("  Sum:     %.2f\n", ctx.sum);
    printf("  Average: %.2f\n", ctx.sum / ctx.count);
    printf("  Min:     %.2f\n", ctx.min);
    printf("  Max:     %.2f\n\n", ctx.max);
}

/* ============================================================================
 * Example 4: Custom Delimiter (Semicolon-separated, European format)
 * ============================================================================ */

static int g_euro_row_count = 0;

static void euro_callback(const csv_row_t *row, void *user_data) {
    (void)user_data;
    g_euro_row_count++;

    if (g_euro_row_count == 1) {
        printf("  Headers: ");
    } else {
        printf("  Data:    ");
    }

    for (size_t i = 0; i < row->num_fields; i++) {
        const csv_field_t *f = csv_get_field(row, i);
        if (i > 0) printf(" | ");
        printf("%.*s", (int)f->size, f->data);
    }
    printf("\n");
}

static void example_custom_delimiter(void) {
    printf("=== Example 4: European CSV (Semicolon Delimiter) ===\n\n");

    const char *csv_data =
        "Name;Betrag;Datum\n"
        "Müller;1.234,56;01.12.2024\n"
        "Schmidt;987,65;15.11.2024\n"
        "Weber;2.500,00;30.10.2024\n";

    csv_parse_options_t opts = csv_default_options();
    opts.delimiter = ';';

    csv_parser_t *parser = csv_parser_create(&opts);
    g_euro_row_count = 0;

    csv_parser_set_row_callback(parser, euro_callback, NULL);
    csv_parse_string(parser, csv_data);
    csv_parser_destroy(parser);

    printf("\n");
}

/* ============================================================================
 * Example 5: Error Handling
 * ============================================================================ */

static void example_error_handling(void) {
    printf("=== Example 5: Error Handling ===\n\n");

    /* Try to parse a non-existent file */
    csv_parser_t *parser = csv_parser_create(NULL);

    printf("Attempting to parse non-existent file:\n");
    csv_error_t err = csv_parse_file(parser, "/nonexistent/file.csv");
    printf("  Result: %s\n", csv_error_string(err));

    csv_parser_destroy(parser);

    /* Parse with strict field size limit */
    printf("\nParsing with small field size limit:\n");

    csv_parse_options_t opts = csv_default_options();
    opts.max_field_size = 5;  /* Very small limit */

    parser = csv_parser_create(&opts);

    err = csv_parse_string(parser, "short,this_field_is_too_long\n");
    printf("  Result: %s\n", csv_error_string(err));

    csv_parser_destroy(parser);
    printf("\n");
}

/* ============================================================================
 * Example 6: File Parsing with Performance Stats
 * ============================================================================ */

static uint64_t g_file_rows = 0;
static uint64_t g_file_fields = 0;

static void file_callback(const csv_row_t *row, void *user_data) {
    (void)user_data;
    g_file_rows++;
    g_file_fields += row->num_fields;
}

static void example_file_parsing(const char *filename) {
    printf("=== Example 6: File Parsing with Stats ===\n\n");
    printf("Parsing: %s\n\n", filename);

    csv_parser_t *parser = csv_parser_create(NULL);
    g_file_rows = 0;
    g_file_fields = 0;

    csv_parser_set_row_callback(parser, file_callback, NULL);

    csv_error_t err = csv_parse_file(parser, filename);

    if (err == CSV_OK) {
        csv_stats_t stats = csv_parser_get_stats(parser);

        printf("Results:\n");
        printf("  Rows parsed:    %llu\n", (unsigned long long)g_file_rows);
        printf("  Fields parsed:  %llu\n", (unsigned long long)g_file_fields);
        printf("  Bytes processed: %llu\n", (unsigned long long)stats.total_bytes_processed);
        printf("  Parse time:     %.3f ms\n", stats.parse_time_ns / 1e6);

        if (stats.parse_time_ns > 0) {
            double mb = stats.total_bytes_processed / (1024.0 * 1024.0);
            double seconds = stats.parse_time_ns / 1e9;
            printf("  Throughput:     %.1f MB/s\n", mb / seconds);
        }
    } else {
        printf("Parse failed: %s\n", csv_error_string(err));
    }

    csv_parser_destroy(parser);
    printf("\n");
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char *argv[]) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║              SonicSV Examples                             ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");

    /* Run in-memory examples */
    example_simple_parsing();
    example_column_extraction();
    example_statistics();
    example_custom_delimiter();
    example_error_handling();

    /* File parsing example */
    if (argc > 1) {
        example_file_parsing(argv[1]);
    } else {
        printf("=== Example 6: File Parsing ===\n\n");
        printf("  Skipped (no file provided)\n");
        printf("  Usage: %s <file.csv>\n\n", argv[0]);
    }

    /* Show SIMD info */
    printf("=== System Info ===\n\n");
    uint32_t simd = csv_get_simd_features();
    printf("  SIMD: ");
    if (simd == CSV_SIMD_NONE) {
        printf("None (scalar fallback)");
    } else {
        if (simd & CSV_SIMD_AVX512) printf("AVX-512 ");
        if (simd & CSV_SIMD_AVX2) printf("AVX2 ");
        if (simd & CSV_SIMD_SSE4_2) printf("SSE4.2 ");
        if (simd & CSV_SIMD_NEON) printf("NEON ");
        if (simd & CSV_SIMD_SVE) printf("SVE ");
    }
    printf("\n\n");

    return 0;
}
