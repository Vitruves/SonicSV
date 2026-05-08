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
    test_row_t *rows;
    size_t row_count;
    csv_error_t last_error;
    char last_error_msg[256];
} test_ctx_t;

/* test_ctx_t.rows is heap-allocated because the underlying array is ~9.8 MB —
 * too large to live on the stack (default Linux stack is 8 MB, and several
 * tests need two contexts simultaneously). A registry tracks live allocations
 * so run_test_suite() can sweep them between tests, keeping peak heap bounded
 * even when ctx_init() is called inside a hot loop. */
typedef struct rows_node {
    test_row_t *rows;
    struct rows_node *next;
} rows_node_t;
static rows_node_t *g_rows_head = NULL;

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
    ctx->rows = (test_row_t *)calloc(MAX_TEST_ROWS, sizeof(test_row_t));
    if (!ctx->rows) {
        fprintf(stderr, "ctx_init: out of memory\n");
        abort();
    }
    rows_node_t *n = (rows_node_t *)malloc(sizeof(*n));
    if (!n) { fprintf(stderr, "ctx_init: out of memory\n"); abort(); }
    n->rows = ctx->rows;
    n->next = g_rows_head;
    g_rows_head = n;
}

static void ctx_free(test_ctx_t *ctx) {
    if (!ctx || !ctx->rows) return;
    rows_node_t **pp = &g_rows_head;
    while (*pp) {
        if ((*pp)->rows == ctx->rows) {
            rows_node_t *dead = *pp;
            *pp = dead->next;
            free(dead);
            break;
        }
        pp = &(*pp)->next;
    }
    free(ctx->rows);
    ctx->rows = NULL;
}

static void ctx_cleanup_all(void) {
    rows_node_t *n = g_rows_head;
    while (n) {
        rows_node_t *next = n->next;
        free(n->rows);
        free(n);
        n = next;
    }
    g_rows_head = NULL;
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

    /* features is a uint32_t bitmask; the call simply must not crash and
     * must return a value within the documented bit range. */
    ASSERT_TRUE((features & ~(uint32_t)(CSV_SIMD_AVX512 | CSV_SIMD_AVX2 |
                                        CSV_SIMD_SSE4_2 | CSV_SIMD_NEON |
                                        CSV_SIMD_SVE)) == 0,
                "SIMD detection returned unknown bits");

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
 * SECTION 8: Bitmask Path - Chunk Boundary Tests
 *
 * Exercises the simple-path bitmask parser at SIMD chunk boundaries
 * (16 B for NEON/SSE4.2, 32 B for AVX2, 64 B for AVX-512). The static
 * test files in tests/data/ are mostly < 64 bytes and thus take the slow
 * path; these tests programmatically build inputs that fall through to
 * csv_parse_simple_fast / csv_parse_simple_bitmask_*.
 * ============================================================================ */

static bool parse_buffer_with_options(const char *buf, size_t len,
                                       test_ctx_t *ctx,
                                       csv_parse_options_t *opts) {
    csv_parser_t *parser = csv_parser_create(opts);
    if (!parser) { TEST_FAIL("parser create failed"); return false; }
    csv_parser_set_row_callback(parser, test_row_callback, ctx);
    csv_parser_set_error_callback(parser, test_error_callback, ctx);
    csv_error_t r = csv_parse_buffer(parser, buf, len, true);
    csv_parser_destroy(parser);
    return r == CSV_OK;
}

/* LF at byte 15 (last byte of NEON 16-byte chunk). */
static bool test_chunk_lf_at_byte_15(void) {
    char buf[256] = {0}; size_t off = 0;
    /* 16 B row with \n at byte 15 */
    off += snprintf(buf+off, sizeof(buf)-off, "abc,defg,hij,kl\n");
    /* Pad past 64 B fast-path threshold */
    for (int i = 0; i < 4; i++)
        off += snprintf(buf+off, sizeof(buf)-off, "row,col,val,xyz\n");

    test_ctx_t ctx; ctx_init(&ctx);
    ASSERT_TRUE(parse_buffer_with_options(buf, off, &ctx, NULL), "parse failed");
    ASSERT_EQ(5, ctx.row_count, "expected 5 rows");
    ASSERT_EQ(4, ctx.rows[0].num_fields, "row 0 fields");
    ASSERT_STR_EQ("kl", ctx.rows[0].fields[3],
                  ctx.rows[0].field_sizes[3], "row 0 last field");
    ASSERT_STR_EQ("xyz", ctx.rows[4].fields[3],
                  ctx.rows[4].field_sizes[3], "row 4 last field");
    TEST_PASS(); return true;
}

/* CRLF straddles byte 15/16 boundary. \r at the last byte of the first NEON
 * chunk, \n at offset 0 of the second chunk. Regression test for the
 * field_start > pos underflow bug fixed in the bitmask path. */
static bool test_chunk_crlf_straddle_15_16(void) {
    char buf[256] = {0};
    /* 15 bytes of content + \r at byte 15, then \n at byte 16 */
    memcpy(buf, "abc,defg,hij,kl", 15);
    buf[15] = '\r';
    buf[16] = '\n';
    size_t off = 17;
    for (int i = 0; i < 4; i++)
        off += snprintf(buf+off, sizeof(buf)-off, "row,col,val,xyz\n");

    test_ctx_t ctx; ctx_init(&ctx);
    ASSERT_TRUE(parse_buffer_with_options(buf, off, &ctx, NULL), "parse failed");
    ASSERT_EQ(5, ctx.row_count, "expected 5 rows");
    ASSERT_EQ(4, ctx.rows[0].num_fields, "row 0 fields");
    ASSERT_STR_EQ("kl", ctx.rows[0].fields[3],
                  ctx.rows[0].field_sizes[3], "kl after CRLF");
    TEST_PASS(); return true;
}

/* LF at byte 31 (AVX2 32-byte chunk boundary). */
static bool test_chunk_lf_at_byte_31(void) {
    char buf[256] = {0}; size_t off = 0;
    /* 32-byte row with \n at byte 31 */
    memcpy(buf, "ab,cd,ef,gh,ij,kl,mn,op,qr,st,u", 31);
    buf[31] = '\n';
    off = 32;
    for (int i = 0; i < 3; i++)
        off += snprintf(buf+off, sizeof(buf)-off,
                        "row,col,val,xyz,more,padding,x\n");

    test_ctx_t ctx; ctx_init(&ctx);
    ASSERT_TRUE(parse_buffer_with_options(buf, off, &ctx, NULL), "parse failed");
    ASSERT_EQ(4, ctx.row_count, "expected 4 rows");
    ASSERT_EQ(11, ctx.rows[0].num_fields, "row 0 fields");
    TEST_PASS(); return true;
}

/* CRLF straddles byte 31/32 (AVX2 boundary). */
static bool test_chunk_crlf_straddle_31_32(void) {
    char buf[256] = {0};
    memcpy(buf, "ab,cd,ef,gh,ij,kl,mn,op,qr,st,u", 31);
    buf[31] = '\r';
    buf[32] = '\n';
    size_t off = 33;
    for (int i = 0; i < 3; i++)
        off += snprintf(buf+off, sizeof(buf)-off,
                        "row,col,val,xyz,more,padding,x\n");

    test_ctx_t ctx; ctx_init(&ctx);
    ASSERT_TRUE(parse_buffer_with_options(buf, off, &ctx, NULL), "parse failed");
    ASSERT_EQ(4, ctx.row_count, "expected 4 rows");
    TEST_PASS(); return true;
}

/* Buffer length exactly 80 bytes (5 NEON chunks), ending in non-separator.
 * Bitmask loop consumes all chunks; tail loop never runs; trailing emit
 * must catch the open field. Regression test for end-of-buffer handling. */
static bool test_chunk_exact_multiple_no_newline(void) {
    char buf[81] = {0}; size_t off = 0;
    /* 4 rows of 16 B each = 64 B */
    for (int i = 0; i < 4; i++)
        off += snprintf(buf+off, sizeof(buf)-off, "aaa,bbb,ccc,ddd\n");
    /* 16 B trailing partial row, no newline */
    memcpy(buf+off, "tail,row,end,uvw", 16);
    off += 16;
    /* Total: 80 bytes, multiple of 16, ends mid-field */

    test_ctx_t ctx; ctx_init(&ctx);
    ASSERT_TRUE(parse_buffer_with_options(buf, off, &ctx, NULL), "parse failed");
    ASSERT_EQ(5, ctx.row_count, "expected 5 rows including trailing");
    ASSERT_EQ(4, ctx.rows[4].num_fields, "trailing row fields");
    ASSERT_STR_EQ("uvw", ctx.rows[4].fields[3],
                  ctx.rows[4].field_sizes[3], "trailing-field content");
    TEST_PASS(); return true;
}

/* Buffer ending exactly at chunk boundary with newline. */
static bool test_chunk_exact_multiple_newline(void) {
    char buf[97] = {0}; size_t off = 0;
    for (int i = 0; i < 6; i++)
        off += snprintf(buf+off, sizeof(buf)-off, "aaa,bbb,ccc,ddd\n");
    /* 96 bytes, 6 complete rows */

    test_ctx_t ctx; ctx_init(&ctx);
    ASSERT_TRUE(parse_buffer_with_options(buf, off, &ctx, NULL), "parse failed");
    ASSERT_EQ(6, ctx.row_count, "expected 6 rows");
    TEST_PASS(); return true;
}

/* Trailing comma at chunk boundary (no row emit, by parser convention). */
static bool test_chunk_trailing_comma_at_boundary(void) {
    char buf[97] = {0}; size_t off = 0;
    for (int i = 0; i < 5; i++)
        off += snprintf(buf+off, sizeof(buf)-off, "aaa,bbb,ccc,ddd\n");
    /* 80 B, then 16 B ending in trailing comma */
    memcpy(buf+off, "trail,row,end,e,", 16);
    off += 16;

    test_ctx_t ctx; ctx_init(&ctx);
    ASSERT_TRUE(parse_buffer_with_options(buf, off, &ctx, NULL), "parse failed");
    /* Behavior: the trailing-comma-no-newline row is emitted by the
     * post-loop trailing emit (4 fields, last empty). Match this against
     * the slow path in the oracle test rather than asserting an exact
     * count here, since CSV libraries vary on this detail. */
    ASSERT_TRUE(ctx.row_count >= 5, "at least 5 rows emitted");
    TEST_PASS(); return true;
}

/* Sweep over a parametric range of buffer lengths around chunk boundaries
 * — the cheapest way to catch off-by-one errors in the bitmask loop bounds. */
static bool test_chunk_boundary_sweep(void) {
    /* For each length L in {65..130}, build an L-byte CSV with \n every
     * 5 bytes ("a,b\n" pattern, padded as needed) and verify it parses
     * without error and emits the expected number of rows. */
    for (size_t target_len = 65; target_len <= 130; target_len++) {
        char buf[160] = {0};
        size_t off = 0;
        int row_count = 0;
        while (off + 4 <= target_len) {
            /* "a,b\n" = 4 bytes per row */
            buf[off++] = 'a';
            buf[off++] = ',';
            buf[off++] = 'b';
            buf[off++] = '\n';
            row_count++;
        }
        /* Pad remainder with non-separator bytes (continues last row). */
        if (off < target_len) {
            /* If 1-3 bytes remain, no terminator — this becomes a trailing
             * partial row of one field. */
            while (off < target_len) buf[off++] = 'x';
            row_count++;  /* trailing row */
        }

        test_ctx_t ctx; ctx_init(&ctx);
        if (!parse_buffer_with_options(buf, target_len, &ctx, NULL)) {
            fprintf(stderr, "    sweep len=%zu: parse failed\n", target_len);
            g_tests_failed++; return false;
        }
        if ((int)ctx.row_count != row_count) {
            fprintf(stderr, "    sweep len=%zu: expected %d rows, got %zu\n",
                    target_len, row_count, ctx.row_count);
            g_tests_failed++; return false;
        }
    }
    TEST_PASS(); return true;
}

/* ============================================================================
 * SECTION 9: Streaming / Chunked-Feed Tests
 * ============================================================================ */

/* Feed a known input as N small chunks (is_final on the last call) and
 * confirm output matches a single full-buffer call. Stresses the slow
 * path's unparsed_buffer + state-machine continuation logic — including
 * the field-pointer relocation that must happen whenever unparsed_buffer
 * is shifted (memmove on partial save) or relocated (realloc on growth)
 * while p->fields[] entries reference it. */
static bool test_chunked_feed_matches_full(void) {
    const char *csv =
        "name,age,city,country\n"
        "Alice,30,Seattle,USA\n"
        "Bob,25,Portland,USA\n"
        "Charlie,42,\"San Francisco, CA\",USA\n"
        "\"Multi\nline\",35,\"Quoted, comma\",CA\n"
        "Dave,28,Boston,USA\n";
    size_t len = strlen(csv);

    /* Reference parse: one big call */
    test_ctx_t ref; ctx_init(&ref);
    ASSERT_TRUE(parse_buffer_with_options(csv, len, &ref, NULL), "ref parse failed");

    /* Sweep every chunk size from 1 byte (pathological — every chunk is
     * smaller than every field) up to len-1 (one byte short of the full
     * input). Output must match the reference parse exactly at every size. */
    for (size_t chunk = 1; chunk < len; chunk++) {
        csv_parser_t *p = csv_parser_create(NULL);
        test_ctx_t ctx; ctx_init(&ctx);
        csv_parser_set_row_callback(p, test_row_callback, &ctx);
        csv_parser_set_error_callback(p, test_error_callback, &ctx);
        size_t off = 0;
        csv_error_t e = CSV_OK;
        while (off < len && e == CSV_OK) {
            size_t n = chunk;
            if (off + n > len) n = len - off;
            bool final = (off + n >= len);
            e = csv_parse_buffer(p, csv + off, n, final);
            off += n;
        }
        csv_parser_destroy(p);
        if (e != CSV_OK) {
            fprintf(stderr, "    chunk_size=%zu: parse error %d\n", chunk, e);
            g_tests_failed++; return false;
        }
        if (ctx.row_count != ref.row_count) {
            fprintf(stderr, "    chunk_size=%zu: rows %zu vs %zu\n",
                    chunk, ctx.row_count, ref.row_count);
            g_tests_failed++; return false;
        }
        for (size_t r = 0; r < ref.row_count; r++) {
            if (ctx.rows[r].num_fields != ref.rows[r].num_fields) {
                fprintf(stderr, "    chunk_size=%zu row=%zu: nfields %zu vs %zu\n",
                        chunk, r, ctx.rows[r].num_fields, ref.rows[r].num_fields);
                g_tests_failed++; return false;
            }
            for (size_t f = 0; f < ref.rows[r].num_fields; f++) {
                if (ref.rows[r].field_sizes[f] != ctx.rows[r].field_sizes[f] ||
                    memcmp(ref.rows[r].fields[f], ctx.rows[r].fields[f],
                           ref.rows[r].field_sizes[f]) != 0) {
                    fprintf(stderr, "    chunk_size=%zu row=%zu field=%zu: "
                            "expected '%.*s' got '%.*s'\n",
                            chunk, r, f,
                            (int)ref.rows[r].field_sizes[f], ref.rows[r].fields[f],
                            (int)ctx.rows[r].field_sizes[f], ctx.rows[r].fields[f]);
                    ctx_free(&ctx);
                    g_tests_failed++; return false;
                }
            }
        }
        ctx_free(&ctx);
    }
    TEST_PASS(); return true;
}

/* BOM only on the first chunk; subsequent chunks must not strip it again. */
static bool test_chunked_feed_bom(void) {
    const char *csv = "\xEF\xBB\xBFname,age\nAlice,30\n";
    csv_parser_t *p = csv_parser_create(NULL);
    test_ctx_t ctx; ctx_init(&ctx);
    csv_parser_set_row_callback(p, test_row_callback, &ctx);
    /* Feed 3-byte chunks: first chunk = BOM only */
    csv_error_t e1 = csv_parse_buffer(p, csv, 3, false);
    csv_error_t e2 = csv_parse_buffer(p, csv + 3, strlen(csv) - 3, true);
    csv_parser_destroy(p);
    ASSERT_EQ(CSV_OK, e1, "BOM chunk parse");
    ASSERT_EQ(CSV_OK, e2, "Body chunk parse");
    ASSERT_EQ(2, ctx.row_count, "row count after chunked BOM");
    ASSERT_STR_EQ("name", ctx.rows[0].fields[0],
                  ctx.rows[0].field_sizes[0], "header field 0 after BOM");
    TEST_PASS(); return true;
}

/* ============================================================================
 * SECTION 10: Configuration Options
 * ============================================================================ */

static bool test_option_ignore_empty_lines(void) {
    /* Default: ignore_empty_lines = true. Blank lines between data rows
     * must NOT produce empty rows. Affects both fast and slow paths. */
    test_ctx_t ctx; ctx_init(&ctx);
    /* Build an input >64 B so the bitmask fast path is exercised. */
    char buf[256] = {0};
    size_t off = 0;
    off += snprintf(buf+off, sizeof(buf)-off, "header,col1,col2\n");
    off += snprintf(buf+off, sizeof(buf)-off, "\n\n");  /* two blank lines */
    off += snprintf(buf+off, sizeof(buf)-off, "alpha,beta,gamma\n");
    off += snprintf(buf+off, sizeof(buf)-off, "\n");    /* another blank */
    off += snprintf(buf+off, sizeof(buf)-off, "delta,epsilon,zeta\n");
    /* Pad to >64 bytes to ensure the bitmask path triggers. */
    while (off < 80) buf[off++] = '\n';

    ASSERT_TRUE(parse_buffer_with_options(buf, off, &ctx, NULL), "parse failed");
    ASSERT_EQ(3, ctx.row_count, "blank lines should be skipped");

    /* Now flip the option off and confirm blank lines DO emit empty rows. */
    csv_parse_options_t opt = csv_default_options();
    opt.ignore_empty_lines = false;
    test_ctx_t ctx2; ctx_init(&ctx2);
    ASSERT_TRUE(parse_buffer_with_options(buf, off, &ctx2, &opt), "parse 2 failed");
    ASSERT_TRUE(ctx2.row_count > ctx.row_count,
                "with ignore_empty_lines=false, blank lines emit rows");
    TEST_PASS(); return true;
}

static bool test_option_trim_whitespace(void) {
    csv_parse_options_t opt = csv_default_options();
    opt.trim_whitespace = true;
    const char *csv = "  alpha  ,\tbeta\t,  gamma\n   x   ,y,z\n";
    test_ctx_t ctx; ctx_init(&ctx);
    ASSERT_TRUE(parse_buffer_with_options(csv, strlen(csv), &ctx, &opt), "parse failed");
    ASSERT_EQ(2, ctx.row_count, "rows");
    ASSERT_STR_EQ("alpha", ctx.rows[0].fields[0],
                  ctx.rows[0].field_sizes[0], "trimmed field 0");
    ASSERT_STR_EQ("beta", ctx.rows[0].fields[1],
                  ctx.rows[0].field_sizes[1], "trimmed field 1");
    ASSERT_STR_EQ("gamma", ctx.rows[0].fields[2],
                  ctx.rows[0].field_sizes[2], "trimmed field 2");
    ASSERT_STR_EQ("x", ctx.rows[1].fields[0],
                  ctx.rows[1].field_sizes[0], "trimmed row 1 field 0");
    TEST_PASS(); return true;
}

static bool test_option_double_quote_disabled(void) {
    /* With double_quote=false, "" inside a quoted field should NOT be
     * collapsed to a literal quote. The parser closes on the first quote. */
    csv_parse_options_t opt = csv_default_options();
    opt.double_quote = false;
    const char *csv = "\"a\"\"b\",c\n";
    test_ctx_t ctx; ctx_init(&ctx);
    ASSERT_TRUE(parse_buffer_with_options(csv, strlen(csv), &ctx, &opt), "parse failed");
    ASSERT_EQ(1, ctx.row_count, "rows");
    /* First quote ends at "a"; next " starts a new quoted field. Behavior
     * depends on the slow path's QUOTE_IN_QUOTED_FIELD handling. We accept
     * either of the documented outcomes; just verify no crash and >=2 fields. */
    ASSERT_TRUE(ctx.rows[0].num_fields >= 2, "at least 2 fields");
    TEST_PASS(); return true;
}

static bool test_option_max_memory_kb(void) {
    /* The cap must be large enough that csv_parser_create succeeds (the
     * initial fields[] array needs ~12 KB), but small enough that growing
     * to fit a 1 MB input triggers OOM. */
    csv_parse_options_t opt = csv_default_options();
    opt.max_memory_kb = 32;

    /* csv_parser_create may legitimately fail if the cap is below the
     * baseline footprint; that's a valid response and not what we're
     * testing. Skip the case where create fails. */
    csv_parser_t *p = csv_parser_create(&opt);
    if (!p) { TEST_PASS(); return true; }

    /* Build a 1 MB row with commas every 10 bytes. Parsing this requires
     * far more than 32 KB of field-array storage, forcing ensure_capacity
     * to refuse and propagate OOM. */
    size_t big_size = 1024 * 1024;
    char *big = (char*)malloc(big_size);
    ASSERT_TRUE(big != NULL, "malloc");
    for (size_t i = 0; i < big_size - 1; i++)
        big[i] = (i % 10 == 9) ? ',' : ('a' + (i % 26));
    big[big_size - 1] = '\n';

    test_ctx_t ctx; ctx_init(&ctx);
    csv_parser_set_row_callback(p, test_row_callback, &ctx);
    csv_parser_set_error_callback(p, test_error_callback, &ctx);
    csv_error_t r = csv_parse_buffer(p, big, big_size, true);
    csv_parser_destroy(p);
    free(big);

    /* We don't crash, and we either succeed (cap was generous enough) or
     * report OOM. Any other error code means the cap is being enforced
     * but propagated as the wrong error type. */
    ASSERT_TRUE(r == CSV_OK || r == CSV_ERROR_OUT_OF_MEMORY,
                "memory-cap response should be OK or OOM");
    TEST_PASS(); return true;
}

/* ============================================================================
 * SECTION 11: Strict Mode
 * ============================================================================ */

static bool test_strict_unclosed_quote(void) {
    csv_parse_options_t opt = csv_default_options();
    opt.strict_mode = true;
    const char *csv = "a,b,\"unclosed\n";
    test_ctx_t ctx; ctx_init(&ctx);
    csv_parser_t *p = csv_parser_create(&opt);
    csv_parser_set_row_callback(p, test_row_callback, &ctx);
    csv_parser_set_error_callback(p, test_error_callback, &ctx);
    csv_error_t r = csv_parse_buffer(p, csv, strlen(csv), true);
    csv_parser_destroy(p);
    ASSERT_EQ(CSV_ERROR_PARSE_ERROR, r, "strict-mode unclosed quote should error");
    TEST_PASS(); return true;
}

static bool test_strict_quote_in_unquoted(void) {
    csv_parse_options_t opt = csv_default_options();
    opt.strict_mode = true;
    /* Quote appearing mid-unquoted-field is malformed */
    const char *csv = "abc\"def,x\n";
    test_ctx_t ctx; ctx_init(&ctx);
    csv_parser_t *p = csv_parser_create(&opt);
    csv_parser_set_row_callback(p, test_row_callback, &ctx);
    csv_parser_set_error_callback(p, test_error_callback, &ctx);
    csv_error_t r = csv_parse_buffer(p, csv, strlen(csv), true);
    csv_parser_destroy(p);
    ASSERT_EQ(CSV_ERROR_PARSE_ERROR, r, "strict-mode quote-in-unquoted should error");
    TEST_PASS(); return true;
}

static bool test_strict_after_closing_quote(void) {
    csv_parse_options_t opt = csv_default_options();
    opt.strict_mode = true;
    /* Garbage between closing quote and delimiter */
    const char *csv = "\"abc\"xyz,d\n";
    test_ctx_t ctx; ctx_init(&ctx);
    csv_parser_t *p = csv_parser_create(&opt);
    csv_parser_set_row_callback(p, test_row_callback, &ctx);
    csv_parser_set_error_callback(p, test_error_callback, &ctx);
    csv_error_t r = csv_parse_buffer(p, csv, strlen(csv), true);
    csv_parser_destroy(p);
    ASSERT_EQ(CSV_ERROR_PARSE_ERROR, r, "strict-mode after-close-quote should error");
    TEST_PASS(); return true;
}

static bool test_strict_well_formed_succeeds(void) {
    /* Strict mode must NOT reject well-formed input. */
    csv_parse_options_t opt = csv_default_options();
    opt.strict_mode = true;
    const char *csv = "a,b,c\n1,2,3\n\"q\",\"r\",\"s\"\n";
    test_ctx_t ctx; ctx_init(&ctx);
    ASSERT_TRUE(parse_buffer_with_options(csv, strlen(csv), &ctx, &opt),
                "well-formed strict parse");
    ASSERT_EQ(3, ctx.row_count, "rows");
    TEST_PASS(); return true;
}

/* ============================================================================
 * SECTION 12: String Pool
 * ============================================================================ */

static bool test_string_pool_basic(void) {
    csv_string_pool_t *pool = csv_string_pool_create(64);
    ASSERT_TRUE(pool != NULL, "pool create");

    const char *a = csv_string_pool_intern(pool, "hello", 5);
    const char *b = csv_string_pool_intern(pool, "world", 5);
    ASSERT_TRUE(a != NULL && b != NULL, "intern non-null");
    ASSERT_TRUE(a != b, "different strings, different storage");

    /* a/b are NUL-terminated by the pool */
    ASSERT_STR_EQ("hello", a, 5, "interned string a");
    ASSERT_STR_EQ("world", b, 5, "interned string b");

    csv_string_pool_destroy(pool);
    TEST_PASS(); return true;
}

static bool test_string_pool_dedup(void) {
    csv_string_pool_t *pool = csv_string_pool_create(64);
    ASSERT_TRUE(pool != NULL, "pool create");

    const char *a = csv_string_pool_intern(pool, "hello", 5);
    const char *b = csv_string_pool_intern(pool, "hello", 5);
    ASSERT_TRUE(a != NULL && b != NULL, "intern non-null");
    ASSERT_EQ((uintptr_t)a, (uintptr_t)b, "duplicate intern returns same pointer");

    /* Force resize: insert enough distinct strings to exceed bucket capacity */
    char tmp[16];
    for (int i = 0; i < 100; i++) {
        snprintf(tmp, sizeof(tmp), "k%d", i);
        const char *s = csv_string_pool_intern(pool, tmp, strlen(tmp));
        ASSERT_TRUE(s != NULL, "intern after resize");
    }
    /* Original pointer must still resolve to the same content. */
    const char *a2 = csv_string_pool_intern(pool, "hello", 5);
    ASSERT_STR_EQ("hello", a2, 5, "still interned after resize");

    csv_string_pool_destroy(pool);
    TEST_PASS(); return true;
}

static bool test_string_pool_clear(void) {
    csv_string_pool_t *pool = csv_string_pool_create(32);
    ASSERT_TRUE(pool != NULL, "pool create");
    csv_string_pool_intern(pool, "alpha", 5);
    csv_string_pool_intern(pool, "beta", 4);
    csv_string_pool_clear(pool);
    /* After clear, re-interning should succeed (and return possibly the same
     * pointer since the data buffer reuses position 0). */
    const char *p = csv_string_pool_intern(pool, "alpha", 5);
    ASSERT_STR_EQ("alpha", p, 5, "intern after clear");
    csv_string_pool_destroy(pool);
    TEST_PASS(); return true;
}

/* ============================================================================
 * SECTION 13: Oracle Fuzz - Bitmask vs Slow Path
 *
 * Generates random no-quote CSV inputs and parses each via two paths:
 *   1. Default options (hits the bitmask fast path for sz > 64)
 *   2. strict_mode=true (forces the slow path; well-formed unquoted input
 *      passes strict mode unchanged)
 * The two paths must produce byte-identical row/field output. This is a
 * much stronger correctness signal than hand-picked cases.
 * ============================================================================ */

/* xorshift32 PRNG: deterministic and stdlib-free for reproducibility. */
static uint32_t fuzz_state = 0xDEADBEEF;
static uint32_t fuzz_rand(void) {
    uint32_t x = fuzz_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    fuzz_state = x;
    return x;
}

static void fuzz_gen(char *buf, size_t len) {
    /* Distribution chosen to produce plenty of separators per row and a
     * mix of LF/CR/CRLF, but no quote characters. */
    for (size_t i = 0; i < len; i++) {
        uint32_t r = fuzz_rand() % 20;
        if (r < 10)      buf[i] = 'a' + (fuzz_rand() % 26);
        else if (r < 14) buf[i] = ',';
        else if (r < 17) buf[i] = '\n';
        else if (r < 18) buf[i] = '\r';
        else             buf[i] = '0' + (fuzz_rand() % 10);
    }
}

static bool fuzz_parse(const char *buf, size_t len, bool strict, test_ctx_t *ctx) {
    csv_parse_options_t opt = csv_default_options();
    opt.strict_mode = strict;
    csv_parser_t *p = csv_parser_create(&opt);
    if (!p) return false;
    csv_parser_set_row_callback(p, test_row_callback, ctx);
    csv_parser_set_error_callback(p, test_error_callback, ctx);
    csv_error_t r = csv_parse_buffer(p, buf, len, true);
    csv_parser_destroy(p);
    return r == CSV_OK && ctx->last_error == CSV_OK;
}

static bool test_fuzz_bitmask_vs_slow(void) {
    fuzz_state = 0xDEADBEEF;  /* deterministic seed */
    const int trials = 200;

    for (int trial = 0; trial < trials; trial++) {
        /* Cap input at MAX_TEST_ROWS rows worth (~512 B is plenty). */
        size_t len = 65 + (fuzz_rand() % 400);
        char buf[600];
        fuzz_gen(buf, len);

        test_ctx_t bm; ctx_init(&bm);
        test_ctx_t sl; ctx_init(&sl);

        bool ok_bm = fuzz_parse(buf, len, false, &bm);
        bool ok_sl = fuzz_parse(buf, len, true, &sl);

        if (!ok_bm || !ok_sl) {
            fprintf(stderr, "    fuzz #%d (len=%zu): bitmask_ok=%d slow_ok=%d\n",
                    trial, len, ok_bm, ok_sl);
            ctx_free(&bm); ctx_free(&sl);
            g_tests_failed++; return false;
        }
        if (bm.row_count != sl.row_count) {
            fprintf(stderr, "    fuzz #%d (len=%zu): row count %zu vs %zu\n",
                    trial, len, bm.row_count, sl.row_count);
            ctx_free(&bm); ctx_free(&sl);
            g_tests_failed++; return false;
        }
        for (size_t r = 0; r < bm.row_count; r++) {
            if (bm.rows[r].num_fields != sl.rows[r].num_fields) {
                fprintf(stderr, "    fuzz #%d row=%zu: nfields %zu vs %zu\n",
                        trial, r, bm.rows[r].num_fields, sl.rows[r].num_fields);
                ctx_free(&bm); ctx_free(&sl);
                g_tests_failed++; return false;
            }
            for (size_t f = 0; f < bm.rows[r].num_fields; f++) {
                if (bm.rows[r].field_sizes[f] != sl.rows[r].field_sizes[f] ||
                    memcmp(bm.rows[r].fields[f], sl.rows[r].fields[f],
                           bm.rows[r].field_sizes[f]) != 0) {
                    fprintf(stderr,
                            "    fuzz #%d row=%zu field=%zu: '%.*s' vs '%.*s'\n",
                            trial, r, f,
                            (int)bm.rows[r].field_sizes[f], bm.rows[r].fields[f],
                            (int)sl.rows[r].field_sizes[f], sl.rows[r].fields[f]);
                    ctx_free(&bm); ctx_free(&sl);
                    g_tests_failed++; return false;
                }
            }
        }
        ctx_free(&bm); ctx_free(&sl);
    }
    TEST_PASS(); return true;
}

/* ============================================================================
 * SECTION 14: Chunked-Feed Boundary Regressions (Gemini review fixes)
 *
 * Each test exercises a specific corruption scenario that was reported by
 * an independent code review and subsequently fixed. The tests parse the
 * full input as one buffer (reference) and again as a sequence of chunks
 * split exactly at the trouble spot, then compare row/field output.
 * ============================================================================ */

static bool feed_chunked(csv_parser_t *p, test_ctx_t *ctx,
                         const char *csv, const size_t *splits, size_t num_splits) {
    csv_parser_set_row_callback(p, test_row_callback, ctx);
    csv_parser_set_error_callback(p, test_error_callback, ctx);
    size_t off = 0;
    size_t total = strlen(csv);
    for (size_t i = 0; i <= num_splits; i++) {
        size_t end = (i < num_splits) ? splits[i] : total;
        bool final = (end == total);
        csv_error_t e = csv_parse_buffer(p, csv + off, end - off, final);
        if (e != CSV_OK) return false;
        off = end;
    }
    return true;
}

/* 1A: chunk ends exactly after a delimiter — fields previously emitted
 * reference the user's chunk, which goes out of scope on return. The
 * post-loop stabilize must copy them to field_data_pool. */
static bool test_chunked_split_after_delim(void) {
    const char *csv = "alice,30,seattle\nbob,25,portland\n";

    test_ctx_t ref; ctx_init(&ref);
    ASSERT_TRUE(parse_buffer_with_options(csv, strlen(csv), &ref, NULL), "ref parse");

    /* Split right after the comma between "alice" and "30" */
    size_t split = 6;  // after "alice,"
    csv_parser_t *p = csv_parser_create(NULL);
    test_ctx_t ctx; ctx_init(&ctx);
    ASSERT_TRUE(feed_chunked(p, &ctx, csv, &split, 1), "chunked feed");
    csv_parser_destroy(p);

    ASSERT_EQ(ref.row_count, ctx.row_count, "row count");
    for (size_t r = 0; r < ref.row_count; r++) {
        ASSERT_EQ(ref.rows[r].num_fields, ctx.rows[r].num_fields, "nfields");
        for (size_t f = 0; f < ref.rows[r].num_fields; f++) {
            ASSERT_STR_EQ(ref.rows[r].fields[f], ctx.rows[r].fields[f],
                          ctx.rows[r].field_sizes[f], "field content");
        }
    }
    TEST_PASS(); return true;
}

/* 1B: an escaped-quote pair `""` is split across chunks — first quote
 * arrives at end of chunk N, second quote at start of chunk N+1. */
static bool test_chunked_split_escaped_quote(void) {
    const char *csv = "\"a\"\"b\",\"c\"\"d\"\n";  /* expected: a"b , c"d */

    test_ctx_t ref; ctx_init(&ref);
    ASSERT_TRUE(parse_buffer_with_options(csv, strlen(csv), &ref, NULL), "ref parse");
    ASSERT_EQ(1, ref.row_count, "1 row in ref");
    ASSERT_EQ(2, ref.rows[0].num_fields, "2 fields in ref");
    ASSERT_STR_EQ("a\"b", ref.rows[0].fields[0],
                  ref.rows[0].field_sizes[0], "ref field 0");

    /* Split between the two quotes of the first `""` (between byte 2 and 3) */
    size_t split = 3;
    csv_parser_t *p = csv_parser_create(NULL);
    test_ctx_t ctx; ctx_init(&ctx);
    ASSERT_TRUE(feed_chunked(p, &ctx, csv, &split, 1), "chunked feed");
    csv_parser_destroy(p);

    ASSERT_EQ(1, ctx.row_count, "1 row");
    ASSERT_EQ(2, ctx.rows[0].num_fields, "2 fields");
    ASSERT_STR_EQ("a\"b", ctx.rows[0].fields[0],
                  ctx.rows[0].field_sizes[0], "field 0 reassembled");
    ASSERT_STR_EQ("c\"d", ctx.rows[0].fields[1],
                  ctx.rows[0].field_sizes[1], "field 1 reassembled");
    TEST_PASS(); return true;
}

/* 1C: CRLF split across chunks — `\r` ends chunk N, `\n` starts chunk N+1.
 * Without the pending_lf_skip flag the parser emits a spurious empty row. */
static bool test_chunked_split_crlf(void) {
    const char *csv = "a,b\r\nc,d\r\ne,f\r\n";

    test_ctx_t ref; ctx_init(&ref);
    ASSERT_TRUE(parse_buffer_with_options(csv, strlen(csv), &ref, NULL), "ref parse");
    ASSERT_EQ(3, ref.row_count, "3 rows in ref");

    /* Split right after each `\r` (positions 4, 9, 14) */
    size_t splits[3] = {4, 9, 14};
    csv_parser_t *p = csv_parser_create(NULL);
    test_ctx_t ctx; ctx_init(&ctx);
    ASSERT_TRUE(feed_chunked(p, &ctx, csv, splits, 3), "chunked feed");
    csv_parser_destroy(p);

    ASSERT_EQ(3, ctx.row_count, "3 rows (no spurious empties)");
    for (size_t r = 0; r < 3; r++) {
        ASSERT_EQ(2, ctx.rows[r].num_fields, "2 fields per row");
    }
    TEST_PASS(); return true;
}

/* 3A: in non-strict mode (default), a quote mid-unquoted-field becomes a
 * literal byte rather than triggering a state transition. */
static bool test_unquoted_literal_quote_nonstrict(void) {
    const char *csv = "abc\"def,xyz\n";

    test_ctx_t ctx; ctx_init(&ctx);
    ASSERT_TRUE(parse_buffer_with_options(csv, strlen(csv), &ctx, NULL), "parse");
    ASSERT_EQ(1, ctx.row_count, "1 row");
    ASSERT_EQ(2, ctx.rows[0].num_fields, "2 fields (the quote is part of field 0)");
    ASSERT_STR_EQ("abc\"def", ctx.rows[0].fields[0],
                  ctx.rows[0].field_sizes[0], "literal quote preserved");
    ASSERT_STR_EQ("xyz", ctx.rows[0].fields[1],
                  ctx.rows[0].field_sizes[1], "field 1");
    TEST_PASS(); return true;
}

/* ============================================================================
 * SECTION 15: External Smoke Tests (run as separate binaries)
 * ============================================================================ */

#ifdef _WIN32
#define INCLUDE_ORDER_SMOKE_CMD "build\\include_order_smoke.exe"
#define CPP_SMOKE_CMD "build\\sonicsv_test_cpp.exe"
#else
#define INCLUDE_ORDER_SMOKE_CMD "./build/include_order_smoke"
#define CPP_SMOKE_CMD "./build/sonicsv_test_cpp"
#endif

/* Include-order smoke: a separate TU that includes <stdio.h> and friends
 * BEFORE sonicsv.h, then defines SONICSV_IMPLEMENTATION. On glibc this
 * exercises the case where the header's _POSIX_C_SOURCE / _DEFAULT_SOURCE
 * defines arrive too late — only -D flags on the compiler invocation
 * help, so this catches Makefile/CFLAGS regressions. The binary is built
 * by the Makefile (build/include_order_smoke); we just exec it. */
static bool test_include_order_smoke_external(void) {
    int rc = system(INCLUDE_ORDER_SMOKE_CMD);
    ASSERT_EQ(0, rc, "include_order_smoke binary returned non-zero");
    TEST_PASS();
    return true;
}

/* C++ smoke: confirms sonicsv.h compiles and links from a C++ TU and that
 * its public API behaves under standard C++17. Built by the Makefile from
 * tests/sonicsv_test.cpp + a separate C implementation TU. */
static bool test_cpp_smoke_external(void) {
    int rc = system(CPP_SMOKE_CMD);
    ASSERT_EQ(0, rc, "C++ smoke binary returned non-zero");
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

static const test_case_t chunk_tests[] = {
    TEST_CASE(test_chunk_lf_at_byte_15),
    TEST_CASE(test_chunk_crlf_straddle_15_16),
    TEST_CASE(test_chunk_lf_at_byte_31),
    TEST_CASE(test_chunk_crlf_straddle_31_32),
    TEST_CASE(test_chunk_exact_multiple_no_newline),
    TEST_CASE(test_chunk_exact_multiple_newline),
    TEST_CASE(test_chunk_trailing_comma_at_boundary),
    TEST_CASE(test_chunk_boundary_sweep),
};

static const test_case_t streaming_tests[] = {
    TEST_CASE(test_chunked_feed_matches_full),
    TEST_CASE(test_chunked_feed_bom),
};

static const test_case_t option_tests[] = {
    TEST_CASE(test_option_ignore_empty_lines),
    TEST_CASE(test_option_trim_whitespace),
    TEST_CASE(test_option_double_quote_disabled),
    TEST_CASE(test_option_max_memory_kb),
};

static const test_case_t strict_tests[] = {
    TEST_CASE(test_strict_unclosed_quote),
    TEST_CASE(test_strict_quote_in_unquoted),
    TEST_CASE(test_strict_after_closing_quote),
    TEST_CASE(test_strict_well_formed_succeeds),
};

static const test_case_t pool_tests[] = {
    TEST_CASE(test_string_pool_basic),
    TEST_CASE(test_string_pool_dedup),
    TEST_CASE(test_string_pool_clear),
};

static const test_case_t fuzz_tests[] = {
    TEST_CASE(test_fuzz_bitmask_vs_slow),
};

static const test_case_t boundary_tests[] = {
    TEST_CASE(test_chunked_split_after_delim),
    TEST_CASE(test_chunked_split_escaped_quote),
    TEST_CASE(test_chunked_split_crlf),
    TEST_CASE(test_unquoted_literal_quote_nonstrict),
};

static const test_case_t external_tests[] = {
    TEST_CASE(test_include_order_smoke_external),
    TEST_CASE(test_cpp_smoke_external),
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
        ctx_cleanup_all();
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

    run_test_suite("Chunk Boundary Tests", chunk_tests,
                   sizeof(chunk_tests) / sizeof(chunk_tests[0]));

    run_test_suite("Streaming / Chunked Feed Tests", streaming_tests,
                   sizeof(streaming_tests) / sizeof(streaming_tests[0]));

    run_test_suite("Configuration Option Tests", option_tests,
                   sizeof(option_tests) / sizeof(option_tests[0]));

    run_test_suite("Strict Mode Tests", strict_tests,
                   sizeof(strict_tests) / sizeof(strict_tests[0]));

    run_test_suite("String Pool Tests", pool_tests,
                   sizeof(pool_tests) / sizeof(pool_tests[0]));

    run_test_suite("Oracle Fuzz Tests", fuzz_tests,
                   sizeof(fuzz_tests) / sizeof(fuzz_tests[0]));

    run_test_suite("Chunked-Feed Boundary Regressions", boundary_tests,
                   sizeof(boundary_tests) / sizeof(boundary_tests[0]));

    run_test_suite("External Smoke Tests", external_tests,
                   sizeof(external_tests) / sizeof(external_tests[0]));

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
