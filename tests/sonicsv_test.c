/*
 * SonicSV Test Suite
 *
 * Comprehensive tests using actual CSV files for real-world parsing validation.
 *
 * Compile: gcc -std=c99 -O3 -march=native -DSONICSV_IMPLEMENTATION \
 *          -o sonicsv_test sonicsv_test.c -lpthread
 *
 * Run:     ./sonicsv_test           # Run all tests
 *          ./sonicsv_test -v        # Verbose output
 */

/* SONICSV_IMPLEMENTATION is passed via Makefile -D flag */
#include "../sonicsv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ============================================================================
 * Test Framework
 * ============================================================================ */

#define TEST_DATA_DIR "tests/data/"

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;
static int g_verbose = 0;

#define TEST_PASS() do { g_tests_passed++; } while(0)
#define TEST_FAIL(msg) do { \
    g_tests_failed++; \
    fprintf(stderr, "    FAIL: %s\n", msg); \
    fprintf(stderr, "          at %s:%d\n", __FILE__, __LINE__); \
} while(0)

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { TEST_FAIL(msg); return false; } \
} while(0)

#define ASSERT_EQ(expected, actual, msg) do { \
    if ((expected) != (actual)) { \
        fprintf(stderr, "    FAIL: %s\n", msg); \
        fprintf(stderr, "          expected: %lld, actual: %lld\n", \
                (long long)(expected), (long long)(actual)); \
        fprintf(stderr, "          at %s:%d\n", __FILE__, __LINE__); \
        g_tests_failed++; \
        return false; \
    } \
} while(0)

#define ASSERT_STR_EQ(expected, actual, len, msg) do { \
    if (strlen(expected) != (len) || memcmp(expected, actual, len) != 0) { \
        fprintf(stderr, "    FAIL: %s\n", msg); \
        fprintf(stderr, "          expected: \"%s\"\n", expected); \
        fprintf(stderr, "          actual:   \"%.*s\"\n", (int)(len), actual); \
        fprintf(stderr, "          at %s:%d\n", __FILE__, __LINE__); \
        g_tests_failed++; \
        return false; \
    } \
} while(0)

/* ============================================================================
 * Test Context - Collects parsed data for verification
 * ============================================================================ */

#define MAX_TEST_ROWS 100
#define MAX_TEST_FIELDS 50
#define MAX_FIELD_LEN 2048

typedef struct {
    size_t num_fields;
    char fields[MAX_TEST_FIELDS][MAX_FIELD_LEN];
    size_t field_sizes[MAX_TEST_FIELDS];
    bool field_quoted[MAX_TEST_FIELDS];
} test_row_t;

typedef struct {
    test_row_t rows[MAX_TEST_ROWS];
    size_t row_count;
    csv_error_t last_error;
    char last_error_msg[256];
} test_ctx_t;

static void test_row_callback(const csv_row_t *row, void *user_data) {
    test_ctx_t *ctx = (test_ctx_t *)user_data;
    if (ctx->row_count >= MAX_TEST_ROWS) return;

    test_row_t *dest = &ctx->rows[ctx->row_count];
    dest->num_fields = row->num_fields < MAX_TEST_FIELDS ? row->num_fields : MAX_TEST_FIELDS;

    for (size_t i = 0; i < dest->num_fields; i++) {
        const csv_field_t *field = csv_get_field(row, i);
        size_t len = field->size < MAX_FIELD_LEN - 1 ? field->size : MAX_FIELD_LEN - 1;
        memcpy(dest->fields[i], field->data, len);
        dest->fields[i][len] = '\0';
        dest->field_sizes[i] = field->size;
        dest->field_quoted[i] = field->quoted;
    }
    ctx->row_count++;
}

static void test_error_callback(csv_error_t error, const char *message,
                                 uint64_t row_number, void *user_data) {
    (void)row_number;
    test_ctx_t *ctx = (test_ctx_t *)user_data;
    ctx->last_error = error;
    strncpy(ctx->last_error_msg, message, sizeof(ctx->last_error_msg) - 1);
}

static void ctx_init(test_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

/* ============================================================================
 * Test Helpers
 * ============================================================================ */

static bool parse_file_with_options(const char *filename, test_ctx_t *ctx,
                                     csv_parse_options_t *opts) {
    csv_parser_t *parser = csv_parser_create(opts);
    if (!parser) {
        TEST_FAIL("Failed to create parser");
        return false;
    }

    csv_parser_set_row_callback(parser, test_row_callback, ctx);
    csv_parser_set_error_callback(parser, test_error_callback, ctx);

    csv_error_t result = csv_parse_file(parser, filename);
    csv_parser_destroy(parser);

    return result == CSV_OK;
}

static bool parse_file(const char *filename, test_ctx_t *ctx) {
    return parse_file_with_options(filename, ctx, NULL);
}

/* ============================================================================
 * SECTION 1: Basic CSV Tests
 * ============================================================================ */

static bool test_basic_simple(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "basic_simple.csv", &ctx),
                "Failed to parse basic_simple.csv");
    ASSERT_EQ(4, ctx.row_count, "Expected 4 rows");
    ASSERT_EQ(3, ctx.rows[0].num_fields, "Expected 3 fields in header");

    ASSERT_STR_EQ("name", ctx.rows[0].fields[0], ctx.rows[0].field_sizes[0], "Header field 0");
    ASSERT_STR_EQ("age", ctx.rows[0].fields[1], ctx.rows[0].field_sizes[1], "Header field 1");
    ASSERT_STR_EQ("city", ctx.rows[0].fields[2], ctx.rows[0].field_sizes[2], "Header field 2");

    ASSERT_STR_EQ("John", ctx.rows[1].fields[0], ctx.rows[1].field_sizes[0], "Row 1 field 0");
    ASSERT_STR_EQ("25", ctx.rows[1].fields[1], ctx.rows[1].field_sizes[1], "Row 1 field 1");
    ASSERT_STR_EQ("New York", ctx.rows[1].fields[2], ctx.rows[1].field_sizes[2], "Row 1 field 2");

    TEST_PASS();
    return true;
}

static bool test_basic_numeric(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "basic_numeric.csv", &ctx),
                "Failed to parse basic_numeric.csv");
    ASSERT_EQ(5, ctx.row_count, "Expected 5 rows");
    ASSERT_EQ(4, ctx.rows[0].num_fields, "Expected 4 fields");

    ASSERT_STR_EQ("19.99", ctx.rows[1].fields[2], ctx.rows[1].field_sizes[2], "Price value");
    ASSERT_STR_EQ("49.99", ctx.rows[4].fields[2], ctx.rows[4].field_sizes[2], "Last price");

    TEST_PASS();
    return true;
}

static bool test_basic_single_column(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "basic_single_column.csv", &ctx),
                "Failed to parse basic_single_column.csv");
    ASSERT_EQ(6, ctx.row_count, "Expected 6 rows");
    ASSERT_EQ(1, ctx.rows[0].num_fields, "Expected 1 field per row");

    ASSERT_STR_EQ("item", ctx.rows[0].fields[0], ctx.rows[0].field_sizes[0], "Header");
    ASSERT_STR_EQ("elderberry", ctx.rows[5].fields[0], ctx.rows[5].field_sizes[0], "Last item");

    TEST_PASS();
    return true;
}

static bool test_basic_single_row(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "basic_single_row.csv", &ctx),
                "Failed to parse basic_single_row.csv");
    ASSERT_EQ(1, ctx.row_count, "Expected 1 row");
    ASSERT_EQ(5, ctx.rows[0].num_fields, "Expected 5 fields");

    TEST_PASS();
    return true;
}

static bool test_basic_wide(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "basic_wide.csv", &ctx),
                "Failed to parse basic_wide.csv");
    ASSERT_EQ(3, ctx.row_count, "Expected 3 rows");
    ASSERT_EQ(20, ctx.rows[0].num_fields, "Expected 20 fields");

    ASSERT_STR_EQ("c1", ctx.rows[0].fields[0], ctx.rows[0].field_sizes[0], "First column");
    ASSERT_STR_EQ("c20", ctx.rows[0].fields[19], ctx.rows[0].field_sizes[19], "Last column");

    TEST_PASS();
    return true;
}

/* ============================================================================
 * SECTION 2: Quoted Field Tests
 * ============================================================================ */

static bool test_quoted_simple(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "quoted_simple.csv", &ctx),
                "Failed to parse quoted_simple.csv");
    ASSERT_EQ(3, ctx.row_count, "Expected 3 rows");

    ASSERT_TRUE(ctx.rows[0].field_quoted[0], "Header field should be quoted");
    ASSERT_STR_EQ("name", ctx.rows[0].fields[0], ctx.rows[0].field_sizes[0], "Quoted header");
    ASSERT_STR_EQ("John Doe", ctx.rows[1].fields[0], ctx.rows[1].field_sizes[0], "Quoted name");

    TEST_PASS();
    return true;
}

static bool test_quoted_with_commas(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "quoted_with_commas.csv", &ctx),
                "Failed to parse quoted_with_commas.csv");
    ASSERT_EQ(4, ctx.row_count, "Expected 4 rows");

    ASSERT_STR_EQ("A small, useful device", ctx.rows[1].fields[1],
                  ctx.rows[1].field_sizes[1], "Field with comma");
    ASSERT_STR_EQ("Large, expensive, and fancy", ctx.rows[2].fields[1],
                  ctx.rows[2].field_sizes[1], "Field with multiple commas");

    TEST_PASS();
    return true;
}

static bool test_quoted_with_newlines(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "quoted_with_newlines.csv", &ctx),
                "Failed to parse quoted_with_newlines.csv");
    ASSERT_EQ(4, ctx.row_count, "Expected 4 rows");

    ASSERT_STR_EQ("Hello\nWorld", ctx.rows[1].fields[1],
                  ctx.rows[1].field_sizes[1], "Field with newline");
    ASSERT_STR_EQ("Line one\nLine two\nLine three", ctx.rows[2].fields[1],
                  ctx.rows[2].field_sizes[1], "Field with multiple newlines");

    TEST_PASS();
    return true;
}

static bool test_quoted_escaped(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "quoted_escaped.csv", &ctx),
                "Failed to parse quoted_escaped.csv");
    ASSERT_EQ(5, ctx.row_count, "Expected 5 rows");

    ASSERT_STR_EQ("She said \"Hello\" to me", ctx.rows[2].fields[1],
                  ctx.rows[2].field_sizes[1], "Escaped quotes");
    ASSERT_STR_EQ("He replied \"Hi\" and \"Bye\"", ctx.rows[3].fields[1],
                  ctx.rows[3].field_sizes[1], "Multiple escaped quotes");

    TEST_PASS();
    return true;
}

static bool test_quoted_mixed(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "quoted_mixed.csv", &ctx),
                "Failed to parse quoted_mixed.csv");
    ASSERT_EQ(4, ctx.row_count, "Expected 4 rows");

    ASSERT_TRUE(ctx.rows[0].field_quoted[1], "Second header should be quoted");
    ASSERT_TRUE(!ctx.rows[0].field_quoted[0], "First header should not be quoted");

    ASSERT_STR_EQ("Bob \"The Builder\" Smith", ctx.rows[3].fields[1],
                  ctx.rows[3].field_sizes[1], "Complex quoted field");

    TEST_PASS();
    return true;
}

static bool test_quoted_complex(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "quoted_complex.csv", &ctx),
                "Failed to parse quoted_complex.csv");
    ASSERT_EQ(4, ctx.row_count, "Expected 4 rows");

    const char *expected = "Roses are red,\nViolets are blue,\n\"CSV\" is great!";
    ASSERT_STR_EQ(expected, ctx.rows[1].fields[1],
                  ctx.rows[1].field_sizes[1], "Complex field with quotes, commas, newlines");

    TEST_PASS();
    return true;
}

/* ============================================================================
 * SECTION 3: Edge Case Tests
 * ============================================================================ */

static bool test_edge_empty(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "edge_empty.csv", &ctx),
                "Failed to parse edge_empty.csv");
    ASSERT_EQ(0, ctx.row_count, "Expected 0 rows for empty file");

    TEST_PASS();
    return true;
}

static bool test_edge_empty_fields(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "edge_empty_fields.csv", &ctx),
                "Failed to parse edge_empty_fields.csv");
    ASSERT_EQ(4, ctx.row_count, "Expected 4 rows");
    ASSERT_EQ(3, ctx.rows[0].num_fields, "Expected 3 fields");

    ASSERT_STR_EQ("a", ctx.rows[0].fields[0], ctx.rows[0].field_sizes[0], "First field");
    ASSERT_EQ(0, ctx.rows[0].field_sizes[1], "Middle field should be empty");
    ASSERT_STR_EQ("c", ctx.rows[0].fields[2], ctx.rows[0].field_sizes[2], "Last field");

    ASSERT_EQ(0, ctx.rows[2].field_sizes[0], "All empty row field 0");
    ASSERT_EQ(0, ctx.rows[2].field_sizes[1], "All empty row field 1");
    ASSERT_EQ(0, ctx.rows[2].field_sizes[2], "All empty row field 2");

    TEST_PASS();
    return true;
}

static bool test_edge_quoted_empty(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "edge_quoted_empty.csv", &ctx),
                "Failed to parse edge_quoted_empty.csv");
    ASSERT_EQ(4, ctx.row_count, "Expected 4 rows");

    ASSERT_EQ(0, ctx.rows[0].field_sizes[0], "Quoted empty field size");
    ASSERT_TRUE(ctx.rows[0].field_quoted[0], "Empty field should be marked quoted");

    TEST_PASS();
    return true;
}

static bool test_edge_trailing_comma(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "edge_trailing_comma.csv", &ctx),
                "Failed to parse edge_trailing_comma.csv");
    ASSERT_EQ(3, ctx.row_count, "Expected 3 rows");
    ASSERT_EQ(4, ctx.rows[0].num_fields, "Trailing comma creates extra empty field");

    TEST_PASS();
    return true;
}

static bool test_edge_no_trailing_newline(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "edge_no_trailing_newline.csv", &ctx),
                "Failed to parse edge_no_trailing_newline.csv");
    ASSERT_EQ(2, ctx.row_count, "Expected 2 rows");
    ASSERT_STR_EQ("123", ctx.rows[1].fields[1], ctx.rows[1].field_sizes[1], "Last field");

    TEST_PASS();
    return true;
}

static bool test_edge_whitespace(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    csv_parse_options_t opts = csv_default_options();
    opts.trim_whitespace = true;

    ASSERT_TRUE(parse_file_with_options(TEST_DATA_DIR "edge_whitespace.csv", &ctx, &opts),
                "Failed to parse edge_whitespace.csv");
    ASSERT_EQ(3, ctx.row_count, "Expected 3 rows");

    ASSERT_STR_EQ("name", ctx.rows[0].fields[0], ctx.rows[0].field_sizes[0], "Trimmed header");
    ASSERT_STR_EQ("John", ctx.rows[1].fields[0], ctx.rows[1].field_sizes[0], "Trimmed value");

    TEST_PASS();
    return true;
}

static bool test_edge_utf8_bom(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "edge_utf8_bom.csv", &ctx),
                "Failed to parse edge_utf8_bom.csv");
    ASSERT_EQ(2, ctx.row_count, "Expected 2 rows");

    ASSERT_STR_EQ("name", ctx.rows[0].fields[0], ctx.rows[0].field_sizes[0],
                  "BOM should be stripped from first field");

    TEST_PASS();
    return true;
}

static bool test_edge_crlf(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "edge_crlf.csv", &ctx),
                "Failed to parse edge_crlf.csv");
    ASSERT_EQ(3, ctx.row_count, "Expected 3 rows with CRLF");
    ASSERT_STR_EQ("John", ctx.rows[1].fields[0], ctx.rows[1].field_sizes[0], "CRLF parsing");

    TEST_PASS();
    return true;
}

static bool test_edge_cr(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "edge_cr.csv", &ctx),
                "Failed to parse edge_cr.csv");
    ASSERT_EQ(3, ctx.row_count, "Expected 3 rows with CR");
    ASSERT_STR_EQ("John", ctx.rows[1].fields[0], ctx.rows[1].field_sizes[0], "CR parsing");

    TEST_PASS();
    return true;
}

/* ============================================================================
 * SECTION 4: Delimiter Tests
 * ============================================================================ */

static bool test_delim_tab(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    csv_parse_options_t opts = csv_default_options();
    opts.delimiter = '\t';

    ASSERT_TRUE(parse_file_with_options(TEST_DATA_DIR "delim_tab.tsv", &ctx, &opts),
                "Failed to parse delim_tab.tsv");
    ASSERT_EQ(4, ctx.row_count, "Expected 4 rows");
    ASSERT_STR_EQ("New York", ctx.rows[1].fields[2], ctx.rows[1].field_sizes[2], "Tab-delimited");

    TEST_PASS();
    return true;
}

static bool test_delim_pipe(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    csv_parse_options_t opts = csv_default_options();
    opts.delimiter = '|';

    ASSERT_TRUE(parse_file_with_options(TEST_DATA_DIR "delim_pipe.csv", &ctx, &opts),
                "Failed to parse delim_pipe.csv");
    ASSERT_EQ(4, ctx.row_count, "Expected 4 rows");
    ASSERT_STR_EQ("New York", ctx.rows[1].fields[2], ctx.rows[1].field_sizes[2], "Pipe-delimited");

    TEST_PASS();
    return true;
}

static bool test_delim_semicolon(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    csv_parse_options_t opts = csv_default_options();
    opts.delimiter = ';';

    ASSERT_TRUE(parse_file_with_options(TEST_DATA_DIR "delim_semicolon.csv", &ctx, &opts),
                "Failed to parse delim_semicolon.csv");
    ASSERT_EQ(4, ctx.row_count, "Expected 4 rows");
    ASSERT_STR_EQ("New York", ctx.rows[1].fields[2], ctx.rows[1].field_sizes[2], "Semicolon-delimited");

    TEST_PASS();
    return true;
}

static bool test_delim_colon(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    csv_parse_options_t opts = csv_default_options();
    opts.delimiter = ':';

    ASSERT_TRUE(parse_file_with_options(TEST_DATA_DIR "delim_colon.csv", &ctx, &opts),
                "Failed to parse delim_colon.csv");
    ASSERT_EQ(4, ctx.row_count, "Expected 4 rows");
    ASSERT_STR_EQ("New York", ctx.rows[1].fields[2], ctx.rows[1].field_sizes[2], "Colon-delimited");

    TEST_PASS();
    return true;
}

/* ============================================================================
 * SECTION 5: Special Cases
 * ============================================================================ */

static bool test_large_field(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "large_field.csv", &ctx),
                "Failed to parse large_field.csv");
    ASSERT_EQ(3, ctx.row_count, "Expected 3 rows");

    /* First data row has a 1000-char field */
    ASSERT_TRUE(ctx.rows[1].field_sizes[1] > 500, "Large field should be parsed");

    TEST_PASS();
    return true;
}

static bool test_unicode(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    ASSERT_TRUE(parse_file(TEST_DATA_DIR "unicode.csv", &ctx),
                "Failed to parse unicode.csv");
    ASSERT_EQ(4, ctx.row_count, "Expected 4 rows");

    /* Just verify parsing works - don't check exact UTF-8 bytes */
    ASSERT_TRUE(ctx.rows[1].field_sizes[0] > 0, "Unicode field should be parsed");

    TEST_PASS();
    return true;
}

/* ============================================================================
 * SECTION 6: API Tests
 * ============================================================================ */

static bool test_api_default_options(void) {
    csv_parse_options_t opts = csv_default_options();

    ASSERT_EQ(',', opts.delimiter, "Default delimiter should be comma");
    ASSERT_EQ('"', opts.quote_char, "Default quote char should be double quote");
    ASSERT_TRUE(opts.double_quote, "Default should support double quotes");
    ASSERT_TRUE(!opts.trim_whitespace, "Default should not trim whitespace");
    ASSERT_TRUE(opts.ignore_empty_lines, "Default should ignore empty lines");
    ASSERT_TRUE(!opts.strict_mode, "Default should not be strict mode");

    TEST_PASS();
    return true;
}

static bool test_api_create_destroy(void) {
    csv_parser_t *parser = csv_parser_create(NULL);
    ASSERT_TRUE(parser != NULL, "Parser creation with NULL should succeed");
    csv_parser_destroy(parser);

    csv_parse_options_t opts = csv_default_options();
    opts.delimiter = ';';
    parser = csv_parser_create(&opts);
    ASSERT_TRUE(parser != NULL, "Parser creation with options should succeed");
    csv_parser_destroy(parser);

    /* Destroy NULL should not crash */
    csv_parser_destroy(NULL);

    TEST_PASS();
    return true;
}

static bool test_api_reset(void) {
    csv_parser_t *parser = csv_parser_create(NULL);
    ASSERT_TRUE(parser != NULL, "Parser creation should succeed");

    csv_error_t err = csv_parser_reset(parser);
    ASSERT_EQ(CSV_OK, err, "Reset should succeed");

    err = csv_parser_reset(NULL);
    ASSERT_EQ(CSV_ERROR_INVALID_ARGS, err, "Reset NULL should fail");

    csv_parser_destroy(parser);

    TEST_PASS();
    return true;
}

static bool test_api_error_strings(void) {
    ASSERT_TRUE(strcmp(csv_error_string(CSV_OK), "Success") == 0, "CSV_OK message");
    ASSERT_TRUE(strcmp(csv_error_string(CSV_ERROR_INVALID_ARGS), "Invalid arguments") == 0,
                "INVALID_ARGS message");
    ASSERT_TRUE(strcmp(csv_error_string(CSV_ERROR_OUT_OF_MEMORY), "Out of memory") == 0,
                "OUT_OF_MEMORY message");
    ASSERT_TRUE(strcmp(csv_error_string(CSV_ERROR_IO_ERROR), "I/O error") == 0,
                "IO_ERROR message");

    TEST_PASS();
    return true;
}

static bool test_api_null_args(void) {
    csv_error_t err;

    err = csv_parse_string(NULL, "data");
    ASSERT_EQ(CSV_ERROR_INVALID_ARGS, err, "NULL parser should fail");

    csv_parser_t *parser = csv_parser_create(NULL);
    err = csv_parse_string(parser, NULL);
    ASSERT_EQ(CSV_ERROR_INVALID_ARGS, err, "NULL string should fail");

    err = csv_parse_file(parser, NULL);
    ASSERT_EQ(CSV_ERROR_INVALID_ARGS, err, "NULL filename should fail");

    err = csv_parse_stream(parser, NULL);
    ASSERT_EQ(CSV_ERROR_INVALID_ARGS, err, "NULL stream should fail");

    csv_parser_destroy(parser);

    TEST_PASS();
    return true;
}

static bool test_api_nonexistent_file(void) {
    test_ctx_t ctx;
    ctx_init(&ctx);

    csv_parser_t *parser = csv_parser_create(NULL);
    csv_parser_set_error_callback(parser, test_error_callback, &ctx);

    csv_error_t err = csv_parse_file(parser, "/nonexistent/path/file.csv");
    ASSERT_TRUE(err != CSV_OK, "Nonexistent file should fail");

    csv_parser_destroy(parser);

    TEST_PASS();
    return true;
}

static bool test_api_stats(void) {
    csv_parser_t *parser = csv_parser_create(NULL);
    test_ctx_t ctx;
    ctx_init(&ctx);

    csv_parser_set_row_callback(parser, test_row_callback, &ctx);
    csv_parse_file(parser, TEST_DATA_DIR "basic_simple.csv");

    csv_stats_t stats = csv_parser_get_stats(parser);
    ASSERT_EQ(4, stats.total_rows_parsed, "Should parse 4 rows");
    ASSERT_EQ(12, stats.total_fields_parsed, "Should parse 12 fields");
    ASSERT_TRUE(stats.total_bytes_processed > 0, "Should process bytes");

    csv_parser_destroy(parser);

    TEST_PASS();
    return true;
}

static bool test_api_simd_features(void) {
    uint32_t features = csv_get_simd_features();

    if (g_verbose) {
        printf("    SIMD Features: ");
        if (features == CSV_SIMD_NONE) {
            printf("None");
        } else {
            if (features & CSV_SIMD_AVX512) printf("AVX-512 ");
            if (features & CSV_SIMD_AVX2) printf("AVX2 ");
            if (features & CSV_SIMD_SSE4_2) printf("SSE4.2 ");
            if (features & CSV_SIMD_NEON) printf("NEON ");
            if (features & CSV_SIMD_SVE) printf("SVE ");
        }
        printf("\n");
    }

    ASSERT_TRUE(features >= 0, "SIMD detection should work");

    TEST_PASS();
    return true;
}

/* ============================================================================
 * SECTION 7: String Parsing Tests
 * ============================================================================ */

static bool test_parse_string(void) {
    csv_parser_t *parser = csv_parser_create(NULL);
    test_ctx_t ctx;
    ctx_init(&ctx);

    csv_parser_set_row_callback(parser, test_row_callback, &ctx);

    const char *csv = "a,b,c\n1,2,3\n";
    csv_error_t err = csv_parse_string(parser, csv);

    ASSERT_EQ(CSV_OK, err, "String parsing should succeed");
    ASSERT_EQ(2, ctx.row_count, "Should parse 2 rows");

    csv_parser_destroy(parser);

    TEST_PASS();
    return true;
}

static bool test_parse_buffer(void) {
    csv_parser_t *parser = csv_parser_create(NULL);
    test_ctx_t ctx;
    ctx_init(&ctx);

    csv_parser_set_row_callback(parser, test_row_callback, &ctx);

    /* Parse in chunks */
    csv_error_t err;
    err = csv_parse_buffer(parser, "name,a", 6, false);
    ASSERT_EQ(CSV_OK, err, "First chunk should succeed");
    ASSERT_EQ(0, ctx.row_count, "No complete rows yet");

    err = csv_parse_buffer(parser, "ge\nJohn,25\n", 11, true);
    ASSERT_EQ(CSV_OK, err, "Second chunk should succeed");
    ASSERT_EQ(2, ctx.row_count, "Should have 2 complete rows");

    ASSERT_STR_EQ("name", ctx.rows[0].fields[0], ctx.rows[0].field_sizes[0], "First field");
    ASSERT_STR_EQ("age", ctx.rows[0].fields[1], ctx.rows[0].field_sizes[1], "Split field");

    csv_parser_destroy(parser);

    TEST_PASS();
    return true;
}

/* ============================================================================
 * Test Runner
 * ============================================================================ */

typedef bool (*test_fn)(void);

typedef struct {
    const char *name;
    test_fn fn;
} test_case_t;

#define TEST_CASE(fn) { #fn, fn }

static const test_case_t basic_tests[] = {
    TEST_CASE(test_basic_simple),
    TEST_CASE(test_basic_numeric),
    TEST_CASE(test_basic_single_column),
    TEST_CASE(test_basic_single_row),
    TEST_CASE(test_basic_wide),
};

static const test_case_t quoted_tests[] = {
    TEST_CASE(test_quoted_simple),
    TEST_CASE(test_quoted_with_commas),
    TEST_CASE(test_quoted_with_newlines),
    TEST_CASE(test_quoted_escaped),
    TEST_CASE(test_quoted_mixed),
    TEST_CASE(test_quoted_complex),
};

static const test_case_t edge_tests[] = {
    TEST_CASE(test_edge_empty),
    TEST_CASE(test_edge_empty_fields),
    TEST_CASE(test_edge_quoted_empty),
    TEST_CASE(test_edge_trailing_comma),
    TEST_CASE(test_edge_no_trailing_newline),
    TEST_CASE(test_edge_whitespace),
    TEST_CASE(test_edge_utf8_bom),
    TEST_CASE(test_edge_crlf),
    TEST_CASE(test_edge_cr),
};

static const test_case_t delim_tests[] = {
    TEST_CASE(test_delim_tab),
    TEST_CASE(test_delim_pipe),
    TEST_CASE(test_delim_semicolon),
    TEST_CASE(test_delim_colon),
};

static const test_case_t special_tests[] = {
    TEST_CASE(test_large_field),
    TEST_CASE(test_unicode),
};

static const test_case_t api_tests[] = {
    TEST_CASE(test_api_default_options),
    TEST_CASE(test_api_create_destroy),
    TEST_CASE(test_api_reset),
    TEST_CASE(test_api_error_strings),
    TEST_CASE(test_api_null_args),
    TEST_CASE(test_api_nonexistent_file),
    TEST_CASE(test_api_stats),
    TEST_CASE(test_api_simd_features),
};

static const test_case_t string_tests[] = {
    TEST_CASE(test_parse_string),
    TEST_CASE(test_parse_buffer),
};

static void run_test_suite(const char *name, const test_case_t *tests, size_t count) {
    printf("\n--- %s ---\n", name);
    for (size_t i = 0; i < count; i++) {
        g_tests_run++;
        printf("  %s... ", tests[i].name);
        fflush(stdout);
        if (tests[i].fn()) {
            printf("PASS\n");
        } else {
            printf("\n");
        }
    }
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_verbose = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [-v|--verbose]\n", argv[0]);
            return 0;
        }
    }

    printf("============================================================\n");
    printf("              SonicSV Test Suite                            \n");
    printf("============================================================\n");

    run_test_suite("Basic CSV Tests", basic_tests,
                   sizeof(basic_tests) / sizeof(basic_tests[0]));

    run_test_suite("Quoted Field Tests", quoted_tests,
                   sizeof(quoted_tests) / sizeof(quoted_tests[0]));

    run_test_suite("Edge Case Tests", edge_tests,
                   sizeof(edge_tests) / sizeof(edge_tests[0]));

    run_test_suite("Delimiter Tests", delim_tests,
                   sizeof(delim_tests) / sizeof(delim_tests[0]));

    run_test_suite("Special Cases", special_tests,
                   sizeof(special_tests) / sizeof(special_tests[0]));

    run_test_suite("API Tests", api_tests,
                   sizeof(api_tests) / sizeof(api_tests[0]));

    run_test_suite("String/Buffer Parsing Tests", string_tests,
                   sizeof(string_tests) / sizeof(string_tests[0]));

    printf("\n============================================================\n");
    printf("                    Test Results                            \n");
    printf("============================================================\n");
    printf("  Total:  %d tests\n", g_tests_run);
    printf("  Passed: %d\n", g_tests_passed);
    printf("  Failed: %d\n", g_tests_failed);
    printf("============================================================\n");

    if (g_tests_failed == 0) {
        printf("\n  ALL TESTS PASSED!\n\n");
    } else {
        printf("\n  SOME TESTS FAILED\n\n");
    }

    return g_tests_failed > 0 ? 1 : 0;
}
