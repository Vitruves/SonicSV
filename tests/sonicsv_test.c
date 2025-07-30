#include "../sonicsv.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

// Test framework macros
#define TEST_ASSERT(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s at %s:%d - %s\n", __func__, __FILE__, __LINE__, message); \
        test_failures++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_EQ(expected, actual, message) do { \
    if ((expected) != (actual)) { \
        fprintf(stderr, "FAIL: %s at %s:%d - %s (expected: %ld, actual: %ld)\n", \
                __func__, __FILE__, __LINE__, message, (long)(expected), (long)(actual)); \
        test_failures++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_STR_EQ(expected, actual, message) do { \
    if (strcmp((expected), (actual)) != 0) { \
        fprintf(stderr, "FAIL: %s at %s:%d - %s (expected: '%s', actual: '%s')\n", \
                __func__, __FILE__, __LINE__, message, expected, actual); \
        test_failures++; \
        return; \
    } \
} while(0)

#define RUN_TEST(test_func) do { \
    printf("Running %s... ", #test_func); \
    fflush(stdout); \
    int failures_before = test_failures; \
    test_func(); \
    if (test_failures == failures_before) { \
        printf("PASS\n"); \
        test_passes++; \
    } \
    test_count++; \
} while(0)

// Global test counters
static int test_count = 0;
static int test_passes = 0;
static int test_failures = 0;

// Test data structures
typedef struct {
    csv_row_t* rows;
    size_t row_count;
    size_t row_capacity;
    csv_error_t last_error;
    char last_error_msg[256];
    uint64_t last_error_row;
} test_context_t;

// Callback functions for testing
static void test_row_callback(const csv_row_t *row, void *user_data) {
    test_context_t *ctx = (test_context_t*)user_data;
    
    // Ensure capacity
    if (ctx->row_count >= ctx->row_capacity) {
        ctx->row_capacity = ctx->row_capacity ? ctx->row_capacity * 2 : 16;
        ctx->rows = realloc(ctx->rows, ctx->row_capacity * sizeof(csv_row_t));
    }
    
    // Deep copy the row data
    csv_row_t *dest_row = &ctx->rows[ctx->row_count++];
    dest_row->num_fields = row->num_fields;
    dest_row->row_number = row->row_number;
    dest_row->byte_offset = row->byte_offset;
    dest_row->fields = malloc(row->num_fields * sizeof(csv_field_t));
    
    for (size_t i = 0; i < row->num_fields; i++) {
        dest_row->fields[i].size = row->fields[i].size;
        dest_row->fields[i].quoted = row->fields[i].quoted;
        dest_row->fields[i].data = malloc(row->fields[i].size + 1);
        memcpy((char*)dest_row->fields[i].data, row->fields[i].data, row->fields[i].size);
        ((char*)dest_row->fields[i].data)[row->fields[i].size] = '\0';
    }
}

static void test_error_callback(csv_error_t error, const char *message, uint64_t row_number, void *user_data) {
    test_context_t *ctx = (test_context_t*)user_data;
    ctx->last_error = error;
    strncpy(ctx->last_error_msg, message, sizeof(ctx->last_error_msg) - 1);
    ctx->last_error_msg[sizeof(ctx->last_error_msg) - 1] = '\0';
    ctx->last_error_row = row_number;
}

static void cleanup_test_context(test_context_t *ctx) {
    if (ctx->rows) {
        for (size_t i = 0; i < ctx->row_count; i++) {
            for (size_t j = 0; j < ctx->rows[i].num_fields; j++) {
                free((char*)ctx->rows[i].fields[j].data);
            }
            free(ctx->rows[i].fields);
        }
        free(ctx->rows);
    }
    memset(ctx, 0, sizeof(test_context_t));
}

// Test functions
static void test_csv_default_options() {
    csv_parse_options_t opts = csv_default_options();
    
    TEST_ASSERT_EQ(',', opts.delimiter, "Default delimiter should be comma");
    TEST_ASSERT_EQ('"', opts.quote_char, "Default quote char should be double quote");
    TEST_ASSERT(opts.double_quote, "Default should support double quotes");
    TEST_ASSERT(!opts.trim_whitespace, "Default should not trim whitespace");
    TEST_ASSERT(opts.ignore_empty_lines, "Default should ignore empty lines");
    TEST_ASSERT(!opts.strict_mode, "Default should not be strict mode");
    TEST_ASSERT(opts.max_field_size > 0, "Default max field size should be positive");
    TEST_ASSERT(opts.buffer_size > 0, "Default buffer size should be positive");
}

static void test_parser_create_destroy() {
    csv_parser_t *parser = csv_parser_create(NULL);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed with NULL options");
    csv_parser_destroy(parser);
    
    csv_parse_options_t opts = csv_default_options();
    parser = csv_parser_create(&opts);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed with valid options");
    csv_parser_destroy(parser);
    
    // Test with NULL - should not crash
    csv_parser_destroy(NULL);
}

static void test_parser_reset() {
    csv_parser_t *parser = csv_parser_create(NULL);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed");
    
    csv_error_t err = csv_parser_reset(parser);
    TEST_ASSERT_EQ(CSV_OK, err, "Parser reset should succeed");
    
    err = csv_parser_reset(NULL);
    TEST_ASSERT_EQ(CSV_ERROR_INVALID_ARGS, err, "Parser reset with NULL should fail");
    
    csv_parser_destroy(parser);
}

static void test_callback_setters() {
    csv_parser_t *parser = csv_parser_create(NULL);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed");
    
    test_context_t ctx = {0};
    
    csv_parser_set_row_callback(parser, test_row_callback, &ctx);
    csv_parser_set_error_callback(parser, test_error_callback, &ctx);
    
    // Test with NULL parser - should not crash
    csv_parser_set_row_callback(NULL, test_row_callback, &ctx);
    csv_parser_set_error_callback(NULL, test_error_callback, &ctx);
    
    csv_parser_destroy(parser);
}

static void test_simple_csv_parsing() {
    csv_parser_t *parser = csv_parser_create(NULL);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed");
    
    test_context_t ctx = {0};
    csv_parser_set_row_callback(parser, test_row_callback, &ctx);
    csv_parser_set_error_callback(parser, test_error_callback, &ctx);
    
    const char *csv_data = "name,age,city\nJohn,25,New York\nJane,30,London\n";
    csv_error_t err = csv_parse_buffer(parser, csv_data, strlen(csv_data), true);
    
    TEST_ASSERT_EQ(CSV_OK, err, "Simple CSV parsing should succeed");
    TEST_ASSERT_EQ(3, ctx.row_count, "Should parse 3 rows");
    
    // Check first row (header)
    TEST_ASSERT_EQ(3, ctx.rows[0].num_fields, "First row should have 3 fields");
    TEST_ASSERT_STR_EQ("name", ctx.rows[0].fields[0].data, "First field should be 'name'");
    TEST_ASSERT_STR_EQ("age", ctx.rows[0].fields[1].data, "Second field should be 'age'");
    TEST_ASSERT_STR_EQ("city", ctx.rows[0].fields[2].data, "Third field should be 'city'");
    
    // Check second row
    TEST_ASSERT_EQ(3, ctx.rows[1].num_fields, "Second row should have 3 fields");
    TEST_ASSERT_STR_EQ("John", ctx.rows[1].fields[0].data, "First field should be 'John'");
    TEST_ASSERT_STR_EQ("25", ctx.rows[1].fields[1].data, "Second field should be '25'");
    TEST_ASSERT_STR_EQ("New York", ctx.rows[1].fields[2].data, "Third field should be 'New York'");
    
    cleanup_test_context(&ctx);
    csv_parser_destroy(parser);
}

static void test_quoted_fields() {
    csv_parser_t *parser = csv_parser_create(NULL);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed");
    
    test_context_t ctx = {0};
    csv_parser_set_row_callback(parser, test_row_callback, &ctx);
    
    const char *csv_data = "\"name\",\"description\"\n\"John Doe\",\"A person with, comma in description\"\n";
    csv_error_t err = csv_parse_buffer(parser, csv_data, strlen(csv_data), true);
    
    TEST_ASSERT_EQ(CSV_OK, err, "Quoted CSV parsing should succeed");
    TEST_ASSERT_EQ(2, ctx.row_count, "Should parse 2 rows");
    
    // Check quoted fields
    TEST_ASSERT(ctx.rows[0].fields[0].quoted, "First field should be marked as quoted");
    TEST_ASSERT_STR_EQ("name", ctx.rows[0].fields[0].data, "First field should be 'name'");
    
    TEST_ASSERT(ctx.rows[1].fields[0].quoted, "Name field should be marked as quoted");
    TEST_ASSERT_STR_EQ("John Doe", ctx.rows[1].fields[0].data, "Name should be 'John Doe'");
    TEST_ASSERT_STR_EQ("A person with, comma in description", ctx.rows[1].fields[1].data, 
                       "Description should contain comma");
    
    cleanup_test_context(&ctx);
    csv_parser_destroy(parser);
}

static void test_escaped_quotes() {
    csv_parser_t *parser = csv_parser_create(NULL);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed");
    
    test_context_t ctx = {0};
    csv_parser_set_row_callback(parser, test_row_callback, &ctx);
    
    const char *csv_data = "\"She said \"\"Hello\"\" to me\",normal\n";
    csv_error_t err = csv_parse_buffer(parser, csv_data, strlen(csv_data), true);
    
    TEST_ASSERT_EQ(CSV_OK, err, "Escaped quotes parsing should succeed");
    TEST_ASSERT_EQ(1, ctx.row_count, "Should parse 1 row");
    TEST_ASSERT_EQ(2, ctx.rows[0].num_fields, "Should have 2 fields");
    TEST_ASSERT_STR_EQ("She said \"Hello\" to me", ctx.rows[0].fields[0].data, 
                       "Escaped quotes should be unescaped");
    
    cleanup_test_context(&ctx);
    csv_parser_destroy(parser);
}

static void test_multiline_quoted_fields() {
    csv_parser_t *parser = csv_parser_create(NULL);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed");
    
    test_context_t ctx = {0};
    csv_parser_set_row_callback(parser, test_row_callback, &ctx);
    
    const char *csv_data = "\"multi\nline\nfield\",\"single line\"\n";
    csv_error_t err = csv_parse_buffer(parser, csv_data, strlen(csv_data), true);
    
    TEST_ASSERT_EQ(CSV_OK, err, "Multiline quoted fields should succeed");
    TEST_ASSERT_EQ(1, ctx.row_count, "Should parse 1 row");
    TEST_ASSERT_STR_EQ("multi\nline\nfield", ctx.rows[0].fields[0].data, 
                       "Multiline field should contain newlines");
    
    cleanup_test_context(&ctx);
    csv_parser_destroy(parser);
}

static void test_empty_fields() {
    csv_parser_t *parser = csv_parser_create(NULL);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed");
    
    test_context_t ctx = {0};
    csv_parser_set_row_callback(parser, test_row_callback, &ctx);
    
    const char *csv_data = "a,,c\n,b,\n\"\",\"\",\"\"\n";
    csv_error_t err = csv_parse_buffer(parser, csv_data, strlen(csv_data), true);
    
    TEST_ASSERT_EQ(CSV_OK, err, "Empty fields parsing should succeed");
    TEST_ASSERT_EQ(3, ctx.row_count, "Should parse 3 rows");
    
    // Check first row
    TEST_ASSERT_STR_EQ("a", ctx.rows[0].fields[0].data, "First field should be 'a'");
    TEST_ASSERT_EQ(0, ctx.rows[0].fields[1].size, "Second field should be empty");
    TEST_ASSERT_STR_EQ("c", ctx.rows[0].fields[2].data, "Third field should be 'c'");
    
    // Check quoted empty fields
    TEST_ASSERT_EQ(0, ctx.rows[2].fields[0].size, "Quoted empty field should be empty");
    TEST_ASSERT(ctx.rows[2].fields[0].quoted, "Empty field should be marked as quoted");
    
    cleanup_test_context(&ctx);
    csv_parser_destroy(parser);
}

static void test_custom_delimiter() {
    csv_parse_options_t opts = csv_default_options();
    opts.delimiter = '|';
    
    csv_parser_t *parser = csv_parser_create(&opts);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed");
    
    test_context_t ctx = {0};
    csv_parser_set_row_callback(parser, test_row_callback, &ctx);
    
    const char *csv_data = "name|age|city\nJohn|25|New York\n";
    csv_error_t err = csv_parse_buffer(parser, csv_data, strlen(csv_data), true);
    
    TEST_ASSERT_EQ(CSV_OK, err, "Custom delimiter parsing should succeed");
    TEST_ASSERT_EQ(2, ctx.row_count, "Should parse 2 rows");
    TEST_ASSERT_STR_EQ("John", ctx.rows[1].fields[0].data, "First field should be 'John'");
    TEST_ASSERT_STR_EQ("25", ctx.rows[1].fields[1].data, "Second field should be '25'");
    
    cleanup_test_context(&ctx);
    csv_parser_destroy(parser);
}

static void test_trim_whitespace() {
    csv_parse_options_t opts = csv_default_options();
    opts.trim_whitespace = true;
    
    csv_parser_t *parser = csv_parser_create(&opts);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed");
    
    test_context_t ctx = {0};
    csv_parser_set_row_callback(parser, test_row_callback, &ctx);
    
    const char *csv_data = " name , age , city \n  John  ,  25  ,  New York  \n";
    csv_error_t err = csv_parse_buffer(parser, csv_data, strlen(csv_data), true);
    
    TEST_ASSERT_EQ(CSV_OK, err, "Trim whitespace parsing should succeed");
    TEST_ASSERT_STR_EQ("name", ctx.rows[0].fields[0].data, "Whitespace should be trimmed");
    TEST_ASSERT_STR_EQ("John", ctx.rows[1].fields[0].data, "Whitespace should be trimmed");
    TEST_ASSERT_STR_EQ("New York", ctx.rows[1].fields[2].data, "Whitespace should be trimmed");
    
    cleanup_test_context(&ctx);
    csv_parser_destroy(parser);
}

static void test_parse_string() {
    csv_parser_t *parser = csv_parser_create(NULL);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed");
    
    test_context_t ctx = {0};
    csv_parser_set_row_callback(parser, test_row_callback, &ctx);
    
    csv_error_t err = csv_parse_string(parser, "a,b,c");
    TEST_ASSERT_EQ(CSV_OK, err, "Parse string should succeed");
    TEST_ASSERT_EQ(1, ctx.row_count, "Should parse 1 row");
    TEST_ASSERT_EQ(3, ctx.rows[0].num_fields, "Row should have 3 fields");
    
    cleanup_test_context(&ctx);
    csv_parser_destroy(parser);
}

static void test_parse_file() {
    // Create a temporary file
    const char *test_file = "/tmp/sonicsv_test.csv";
    FILE *f = fopen(test_file, "w");
    TEST_ASSERT(f != NULL, "Should be able to create test file");
    
    fprintf(f, "name,age,city\nJohn,25,New York\nJane,30,London\n");
    fclose(f);
    
    csv_parser_t *parser = csv_parser_create(NULL);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed");
    
    test_context_t ctx = {0};
    csv_parser_set_row_callback(parser, test_row_callback, &ctx);
    
    csv_error_t err = csv_parse_file(parser, test_file);
    TEST_ASSERT_EQ(CSV_OK, err, "Parse file should succeed");
    TEST_ASSERT_EQ(3, ctx.row_count, "Should parse 3 rows");
    
    // Test non-existent file
    err = csv_parse_file(parser, "/tmp/nonexistent_file.csv");
    TEST_ASSERT(err != CSV_OK, "Non-existent file should fail");
    
    cleanup_test_context(&ctx);
    csv_parser_destroy(parser);
    unlink(test_file);
}

static void test_parse_stream() {
    // Create a temporary file
    const char *test_file = "/tmp/sonicsv_test_stream.csv";
    FILE *f = fopen(test_file, "w");
    TEST_ASSERT(f != NULL, "Should be able to create test file");
    
    fprintf(f, "name,age\nJohn,25\nJane,30\n");
    fclose(f);
    
    csv_parser_t *parser = csv_parser_create(NULL);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed");
    
    test_context_t ctx = {0};
    csv_parser_set_row_callback(parser, test_row_callback, &ctx);
    
    f = fopen(test_file, "r");
    TEST_ASSERT(f != NULL, "Should be able to open test file for reading");
    
    csv_error_t err = csv_parse_stream(parser, f);
    fclose(f);
    
    TEST_ASSERT_EQ(CSV_OK, err, "Parse stream should succeed");
    TEST_ASSERT_EQ(3, ctx.row_count, "Should parse 3 rows");
    
    cleanup_test_context(&ctx);
    csv_parser_destroy(parser);
    unlink(test_file);
}

static void test_error_handling() {
    csv_parse_options_t opts = csv_default_options();
    opts.max_field_size = 10; // Very small limit
    opts.strict_mode = true;
    
    csv_parser_t *parser = csv_parser_create(&opts);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed");
    
    test_context_t ctx = {0};
    csv_parser_set_row_callback(parser, test_row_callback, &ctx);
    csv_parser_set_error_callback(parser, test_error_callback, &ctx);
    
    // Test field too large
    const char *large_field = "this is a very long field that exceeds the limit,normal";
    csv_error_t err = csv_parse_buffer(parser, large_field, strlen(large_field), true);
    
    TEST_ASSERT(err != CSV_OK, "Large field should cause error");
    TEST_ASSERT_EQ(CSV_ERROR_FIELD_TOO_LARGE, ctx.last_error, "Should report field too large error");
    
    cleanup_test_context(&ctx);
    csv_parser_destroy(parser);
}

static void test_get_stats() {
    csv_parser_t *parser = csv_parser_create(NULL);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed");
    
    test_context_t ctx = {0};
    csv_parser_set_row_callback(parser, test_row_callback, &ctx);
    
    const char *csv_data = "a,b,c\n1,2,3\n4,5,6\n";
    csv_parse_buffer(parser, csv_data, strlen(csv_data), true);
    
    csv_stats_t stats = csv_parser_get_stats(parser);
    TEST_ASSERT_EQ(3, stats.total_rows_parsed, "Should report 3 rows parsed");
    TEST_ASSERT_EQ(9, stats.total_fields_parsed, "Should report 9 fields parsed");
    TEST_ASSERT(stats.total_bytes_processed > 0, "Should report bytes processed");
    
    cleanup_test_context(&ctx);
    csv_parser_destroy(parser);
}

static void test_get_field_functions() {
    csv_parser_t *parser = csv_parser_create(NULL);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed");
    
    test_context_t ctx = {0};
    csv_parser_set_row_callback(parser, test_row_callback, &ctx);
    
    csv_parse_string(parser, "a,b,c");
    
    TEST_ASSERT_EQ(1, ctx.row_count, "Should have 1 row");
    csv_row_t *row = &ctx.rows[0];
    
    TEST_ASSERT_EQ(3, csv_get_num_fields(row), "Should report 3 fields");
    
    const csv_field_t *field = csv_get_field(row, 0);
    TEST_ASSERT(field != NULL, "Should get first field");
    TEST_ASSERT_EQ(1, field->size, "First field size should be 1");
    
    field = csv_get_field(row, 10);
    TEST_ASSERT(field == NULL, "Out of bounds field should return NULL");
    
    TEST_ASSERT_EQ(0, csv_get_num_fields(NULL), "NULL row should return 0 fields");
    TEST_ASSERT(csv_get_field(NULL, 0) == NULL, "NULL row should return NULL field");
    
    cleanup_test_context(&ctx);
    csv_parser_destroy(parser);
}

static void test_error_string_function() {
    TEST_ASSERT_STR_EQ("Success", csv_error_string(CSV_OK), "Should return success message");
    TEST_ASSERT_STR_EQ("Invalid arguments", csv_error_string(CSV_ERROR_INVALID_ARGS), 
                       "Should return invalid args message");
    TEST_ASSERT_STR_EQ("Out of memory", csv_error_string(CSV_ERROR_OUT_OF_MEMORY), 
                       "Should return out of memory message");
}

static void test_chunked_parsing() {
    csv_parser_t *parser = csv_parser_create(NULL);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed");
    
    test_context_t ctx = {0};
    csv_parser_set_row_callback(parser, test_row_callback, &ctx);
    
    // Parse data in chunks
    csv_error_t err = csv_parse_buffer(parser, "name,a", 6, false);
    TEST_ASSERT_EQ(CSV_OK, err, "First chunk should succeed");
    TEST_ASSERT_EQ(0, ctx.row_count, "Should not have complete rows yet");
    
    err = csv_parse_buffer(parser, "ge\nJohn,25\n", 11, true);
    TEST_ASSERT_EQ(CSV_OK, err, "Second chunk should succeed");
    TEST_ASSERT_EQ(2, ctx.row_count, "Should have 2 complete rows");
    
    TEST_ASSERT_STR_EQ("name", ctx.rows[0].fields[0].data, "First field should be 'name'");
    TEST_ASSERT_STR_EQ("age", ctx.rows[0].fields[1].data, "Second field should be 'age'");
    
    cleanup_test_context(&ctx);
    csv_parser_destroy(parser);
}

static void test_large_dataset() {
    csv_parser_t *parser = csv_parser_create(NULL);
    TEST_ASSERT(parser != NULL, "Parser creation should succeed");
    
    test_context_t ctx = {0};
    csv_parser_set_row_callback(parser, test_row_callback, &ctx);
    
    // Create a large CSV string
    char *large_csv = malloc(100000);
    strcpy(large_csv, "id,name,value\n");
    char *pos = large_csv + strlen(large_csv);
    
    for (int i = 0; i < 1000; i++) {
        pos += sprintf(pos, "%d,item_%d,%d\n", i, i, i * 10);
    }
    
    csv_error_t err = csv_parse_buffer(parser, large_csv, strlen(large_csv), true);
    TEST_ASSERT_EQ(CSV_OK, err, "Large dataset parsing should succeed");
    TEST_ASSERT_EQ(1001, ctx.row_count, "Should parse 1001 rows (including header)");
    
    free(large_csv);
    cleanup_test_context(&ctx);
    csv_parser_destroy(parser);
}

// Main test runner
int main() {
    printf("Running SonicSV Core API Tests\n");
    printf("==============================\n\n");
    
    // Basic API tests
    RUN_TEST(test_csv_default_options);
    RUN_TEST(test_parser_create_destroy);
    RUN_TEST(test_parser_reset);
    RUN_TEST(test_callback_setters);
    RUN_TEST(test_error_string_function);
    
    // Parsing functionality tests
    RUN_TEST(test_simple_csv_parsing);
    RUN_TEST(test_quoted_fields);
    RUN_TEST(test_escaped_quotes);
    RUN_TEST(test_multiline_quoted_fields);
    RUN_TEST(test_empty_fields);
    RUN_TEST(test_custom_delimiter);
    RUN_TEST(test_trim_whitespace);
    
    // Different parsing methods
    RUN_TEST(test_parse_string);
    RUN_TEST(test_parse_file);
    RUN_TEST(test_parse_stream);
    RUN_TEST(test_chunked_parsing);
    
    // Utility functions
    RUN_TEST(test_get_stats);
    RUN_TEST(test_get_field_functions);
    
    // Error handling and edge cases
    RUN_TEST(test_error_handling);
    RUN_TEST(test_large_dataset);
    
    printf("\n==============================\n");
    printf("Test Results: %d/%d passed", test_passes, test_count);
    if (test_failures > 0) {
        printf(" (%d failed)", test_failures);
    }
    printf("\n");
    
    return test_failures > 0 ? 1 : 0;
}