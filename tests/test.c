// test_sonicsv_extended.c
#define SONICSV_IMPLEMENTATION
#include "../include/sonicsv.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <signal.h>
#include <setjmp.h>

// Test data constants
static const char* CSV_SIMPLE = "name,age,city\nJohn,25,Paris\nJane,30,London\n";
static const char* CSV_QUOTED = "\"name\",\"age\",\"city\"\n\"John Doe\",\"25\",\"Paris, France\"\n\"Jane Smith\",\"30\",\"London, UK\"\n";
static const char* CSV_MIXED_QUOTES = "name,\"age with spaces\",city\nJohn,\"25 years\",Paris\n\"Jane\",30,\"New York\"\n";
static const char* CSV_EMPTY_FIELDS = "name,age,city\nJohn,,Paris\n,30,London\n\n";
static const char* CSV_UNICODE = "名前,年齢,都市\n田中,25,東京\nSmith,30,ニューヨーク\n";
static const char* TSV_DATA = "name\tage\tcity\nJohn\t25\tParis\nJane\t30\tLondon\n";
static const char* CSV_CRLF = "name,age,city\r\nJohn,25,Paris\r\nJane,30,London\r\n";
static const char* CSV_ESCAPED = "name,description,value\nTest,\"Value with \"\"quotes\"\"\",123\nOther,Simple value,456\n";

// Global test state
static int total_tests = 0;
static int passed_tests = 0;
static int failed_tests = 0;
static char error_buffer[1024];

// SIMD detection and validation
static jmp_buf simd_test_env;
static bool simd_test_active = false;

void simd_signal_handler(int sig) {
    if (simd_test_active) {
        simd_test_active = false;
        longjmp(simd_test_env, 1);
    }
}

void setup_simd_test_environment(void) {
    signal(SIGILL, simd_signal_handler);
    signal(SIGSEGV, simd_signal_handler);
}

bool test_simd_instruction_safety(void) {
    setup_simd_test_environment();
    
    if (setjmp(simd_test_env) == 0) {
        simd_test_active = true;
        
        // Try basic SSE operations
        #ifdef HAVE_SSE4_2
        if (csv_detect_simd_features() & CSV_SIMD_SSE4_2) {
            char test_data[16] = "hello,world,test";
            csv_search_result_t result = csv_simd_find_special_char(test_data, 16, ',', '"', '\\');
            (void)result;
        }
        #endif
        
        #ifdef HAVE_AVX2
        if (csv_detect_simd_features() & CSV_SIMD_AVX2) {
            char test_data[32] = "hello,world,test,data,more,stuff";
            uint32_t count = csv_simd_count_delimiters(test_data, 32, ',');
            (void)count;
        }
        #endif
        
        simd_test_active = false;
        return true;
    } else {
        printf("Warning: SIMD instruction caused exception, falling back to scalar\n");
        return false;
    }
}

// Test utilities
#define TEST(name) \
    do { \
        total_tests++; \
        printf("Running test: %s... ", #name); \
        fflush(stdout); \
        if (test_##name()) { \
            printf("✓ PASS\n"); \
            passed_tests++; \
        } else { \
            printf("✗ FAIL - %s\n", error_buffer); \
            failed_tests++; \
        } \
    } while(0)

#define ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            snprintf(error_buffer, sizeof(error_buffer), "%s", message); \
            return false; \
        } \
    } while(0)

#define ASSERT_EQ(expected, actual, message) \
    do { \
        if ((expected) != (actual)) { \
            snprintf(error_buffer, sizeof(error_buffer), \
                "%s (expected: %d, actual: %d)", message, (int)(expected), (int)(actual)); \
            return false; \
        } \
    } while(0)

#define ASSERT_STR_EQ(expected, actual, len, message) \
    do { \
        if (strncmp(expected, actual, len) != 0) { \
            snprintf(error_buffer, sizeof(error_buffer), \
                "%s (expected: '%.*s', actual: '%.*s')", \
                message, (int)(len), expected, (int)(len), actual); \
            return false; \
        } \
    } while(0)

// Test callback data structures
typedef struct {
    int row_count;
    int field_count;
    char last_field[256];
    bool has_error;
    csv_error_t last_error;
} test_callback_data_t;

void test_row_callback(const csv_row_t* row, void* user_data) {
    test_callback_data_t* data = (test_callback_data_t*)user_data;
    data->row_count++;
    data->field_count += row->num_fields;
    
    if (row->num_fields > 0) {
        uint32_t len = row->fields[row->num_fields - 1].size;
        if (len < sizeof(data->last_field) - 1) {
            memcpy(data->last_field, row->fields[row->num_fields - 1].data, len);
            data->last_field[len] = '\0';
        }
    }
}

void test_error_callback(csv_error_t error, const char* message, uint64_t row_number, void* user_data) {
    test_callback_data_t* data = (test_callback_data_t*)user_data;
    data->has_error = true;
    data->last_error = error;
    (void)message;
    (void)row_number;
}

void test_progress_callback(uint64_t bytes_processed, uint64_t total_bytes, void* user_data) {
    (void)bytes_processed;
    (void)total_bytes;
    (void)user_data;
}

// Test functions
bool test_simd_feature_detection(void) {
    uint32_t features = csv_detect_simd_features();
    
    printf("\n  Detected SIMD features: ");
    if (features & CSV_SIMD_SSE4_2) printf("SSE4.2 ");
    if (features & CSV_SIMD_AVX2) printf("AVX2 ");
    if (features & CSV_SIMD_NEON) printf("NEON ");
    if (features == CSV_SIMD_NONE) printf("None");
    printf("\n  ");
    
    csv_simd_init();
    uint32_t runtime_features = csv_simd_get_features();
    
    ASSERT_EQ(features, runtime_features, "Runtime feature detection mismatch");
    
    return test_simd_instruction_safety();
}

bool test_basic_parsing(void) {
    test_callback_data_t data = {0};
    
    csv_parser_t* parser = csv_parser_create(NULL);
    ASSERT(parser != NULL, "Failed to create parser");
    
    csv_parser_set_row_callback(parser, test_row_callback, &data);
    csv_parser_set_error_callback(parser, test_error_callback, &data);
    
    csv_error_t err = csv_parse_buffer(parser, CSV_SIMPLE, strlen(CSV_SIMPLE), true);
    ASSERT_EQ(CSV_OK, err, "Parse error");
    ASSERT_EQ(3, data.row_count, "Wrong row count");
    ASSERT_EQ(9, data.field_count, "Wrong field count");
    ASSERT(!data.has_error, "Unexpected error during parsing");
    
    csv_parser_destroy(parser);
    return true;
}

bool test_quoted_fields(void) {
    test_callback_data_t data = {0};
    
    csv_parser_t* parser = csv_parser_create(NULL);
    ASSERT(parser != NULL, "Failed to create parser");
    
    csv_parser_set_row_callback(parser, test_row_callback, &data);
    
    csv_error_t err = csv_parse_buffer(parser, CSV_QUOTED, strlen(CSV_QUOTED), true);
    ASSERT_EQ(CSV_OK, err, "Parse error");
    ASSERT_EQ(3, data.row_count, "Wrong row count");
    
    csv_parser_destroy(parser);
    return true;
}

bool test_mixed_quoting(void) {
    test_callback_data_t data = {0};
    
    csv_parser_t* parser = csv_parser_create(NULL);
    ASSERT(parser != NULL, "Failed to create parser");
    
    csv_parser_set_row_callback(parser, test_row_callback, &data);
    
    csv_error_t err = csv_parse_buffer(parser, CSV_MIXED_QUOTES, strlen(CSV_MIXED_QUOTES), true);
    ASSERT_EQ(CSV_OK, err, "Parse error");
    ASSERT_EQ(3, data.row_count, "Wrong row count");
    
    csv_parser_destroy(parser);
    return true;
}

bool test_empty_fields(void) {
    test_callback_data_t data = {0};
    
    csv_parser_t* parser = csv_parser_create(NULL);
    ASSERT(parser != NULL, "Failed to create parser");
    
    csv_parser_set_row_callback(parser, test_row_callback, &data);
    
    csv_error_t err = csv_parse_buffer(parser, CSV_EMPTY_FIELDS, strlen(CSV_EMPTY_FIELDS), true);
    ASSERT_EQ(CSV_OK, err, "Parse error");
    ASSERT_EQ(3, data.row_count, "Wrong row count (should ignore empty line)");
    
    csv_parser_destroy(parser);
    return true;
}

bool test_unicode_support(void) {
    test_callback_data_t data = {0};
    
    csv_parser_t* parser = csv_parser_create(NULL);
    ASSERT(parser != NULL, "Failed to create parser");
    
    csv_parser_set_row_callback(parser, test_row_callback, &data);
    
    csv_error_t err = csv_parse_buffer(parser, CSV_UNICODE, strlen(CSV_UNICODE), true);
    ASSERT_EQ(CSV_OK, err, "Parse error");
    ASSERT_EQ(3, data.row_count, "Wrong row count");
    
    bool is_valid = csv_simd_validate_utf8(CSV_UNICODE, strlen(CSV_UNICODE));
    ASSERT(is_valid, "UTF-8 validation failed");
    
    csv_parser_destroy(parser);
    return true;
}

bool test_tsv_parsing(void) {
    test_callback_data_t data = {0};
    
    csv_parse_options_t opts = csv_default_options();
    opts.delimiter = '\t';
    
    csv_parser_t* parser = csv_parser_create(&opts);
    ASSERT(parser != NULL, "Failed to create parser");
    
    csv_parser_set_row_callback(parser, test_row_callback, &data);
    
    csv_error_t err = csv_parse_buffer(parser, TSV_DATA, strlen(TSV_DATA), true);
    ASSERT_EQ(CSV_OK, err, "Parse error");
    ASSERT_EQ(3, data.row_count, "Wrong row count");
    
    csv_parser_destroy(parser);
    return true;
}

bool test_crlf_handling(void) {
    test_callback_data_t data = {0};
    
    csv_parser_t* parser = csv_parser_create(NULL);
    ASSERT(parser != NULL, "Failed to create parser");
    
    csv_parser_set_row_callback(parser, test_row_callback, &data);
    
    csv_error_t err = csv_parse_buffer(parser, CSV_CRLF, strlen(CSV_CRLF), true);
    ASSERT_EQ(CSV_OK, err, "Parse error");
    ASSERT_EQ(3, data.row_count, "Wrong row count");
    
    csv_parser_destroy(parser);
    return true;
}

bool test_escaped_quotes(void) {
    test_callback_data_t data = {0};
    
    csv_parser_t* parser = csv_parser_create(NULL);
    ASSERT(parser != NULL, "Failed to create parser");
    
    csv_parser_set_row_callback(parser, test_row_callback, &data);
    
    csv_error_t err = csv_parse_buffer(parser, CSV_ESCAPED, strlen(CSV_ESCAPED), true);
    ASSERT_EQ(CSV_OK, err, "Parse error");
    ASSERT_EQ(3, data.row_count, "Wrong row count");
    
    csv_parser_destroy(parser);
    return true;
}

bool test_mode_detection(void) {
    csv_parse_mode_t simple_mode = csv_detect_parse_mode(CSV_SIMPLE, strlen(CSV_SIMPLE));
    csv_parse_mode_t quoted_mode = csv_detect_parse_mode(CSV_QUOTED, strlen(CSV_QUOTED));
    csv_parse_mode_t tsv_mode = csv_detect_parse_mode(TSV_DATA, strlen(TSV_DATA));
    
    ASSERT_EQ(CSV_MODE_TSV, tsv_mode, "TSV mode detection failed");
    ASSERT(simple_mode == CSV_MODE_SIMPLE || simple_mode == CSV_MODE_GENERIC, "Simple mode detection failed");
    
    return true;
}

bool test_simd_operations(void) {
    if (!test_simd_instruction_safety()) {
        printf("\n  Skipping SIMD operations test due to instruction safety issues\n  ");
        return true;
    }
    
    const char* test_data = "hello,world,test\nfoo,bar,baz\n";
    size_t data_size = strlen(test_data);
    
    csv_search_result_t result = csv_simd_find_special_char(test_data, data_size, ',', '"', '\\');
    ASSERT(result.pos != NULL && *result.pos == ',', "SIMD special char search failed");
    
    uint32_t comma_count = csv_simd_count_delimiters(test_data, data_size, ',');
    ASSERT_EQ(4, comma_count, "SIMD delimiter count failed");
    
    csv_search_result_t eol_result = csv_simd_find_eol(test_data, data_size);
    ASSERT(eol_result.pos != NULL && *eol_result.pos == '\n', "SIMD EOL search failed");
    
    return true;
}

bool test_block_parser(void) {
    csv_block_config_t config = csv_default_block_config();
    csv_block_parser_t* parser = csv_block_parser_create(&config);
    ASSERT(parser != NULL, "Failed to create block parser");
    
    csv_error_t err = csv_block_parse_buffer(parser, CSV_SIMPLE, strlen(CSV_SIMPLE), true);
    ASSERT_EQ(CSV_OK, err, "Block parse error");
    
    csv_block_parser_destroy(parser);
    return true;
}

bool test_large_file_simulation(void) {
    const size_t NUM_ROWS = 10000;
    const char* row_template = "%zu,User%zu,user%zu@example.com\n";
    
    size_t estimated_size = strlen("id,name,email\n") + NUM_ROWS * 50;
    char* large_data = malloc(estimated_size);
    ASSERT(large_data != NULL, "Failed to allocate memory for large file test");
    
    strcpy(large_data, "id,name,email\n");
    char* pos = large_data + strlen("id,name,email\n");
    
    for (size_t i = 0; i < NUM_ROWS; i++) {
        int written = sprintf(pos, row_template, i, i, i);
        pos += written;
    }
    
    test_callback_data_t data = {0};
    csv_parser_t* parser = csv_parser_create(NULL);
    ASSERT(parser != NULL, "Failed to create parser");
    
    csv_parser_set_row_callback(parser, test_row_callback, &data);
    csv_parser_set_progress_callback(parser, test_progress_callback, &data);
    
    csv_error_t err = csv_parse_buffer(parser, large_data, pos - large_data, true);
    ASSERT_EQ(CSV_OK, err, "Large file parse error");
    ASSERT_EQ((int)(NUM_ROWS + 1), data.row_count, "Wrong row count for large file");
    
    csv_stats_t stats = csv_parser_get_stats(parser);
    printf("\n  Large file stats: %.2f MB/s, %lu rows\n  ", stats.throughput_mbps, stats.total_rows_parsed);
    
    csv_parser_destroy(parser);
    free(large_data);
    return true;
}

bool test_file_operations(void) {
    const char* filename = "test_temp.csv";
    
    FILE* file = fopen(filename, "w");
    ASSERT(file != NULL, "Failed to create test file");
    fwrite(CSV_SIMPLE, 1, strlen(CSV_SIMPLE), file);
    fclose(file);
    
    test_callback_data_t data = {0};
    csv_parser_t* parser = csv_parser_create(NULL);
    ASSERT(parser != NULL, "Failed to create parser");
    
    csv_parser_set_row_callback(parser, test_row_callback, &data);
    
    csv_error_t err = csv_parse_file(parser, filename);
    ASSERT_EQ(CSV_OK, err, "File parse error");
    ASSERT_EQ(3, data.row_count, "Wrong row count from file");
    
    csv_parser_destroy(parser);
    unlink(filename);
    return true;
}

bool test_string_pool(void) {
    csv_string_pool_t* pool = csv_string_pool_create(1024);
    ASSERT(pool != NULL, "Failed to create string pool");
    
    const char* str1 = csv_string_pool_intern(pool, "hello", 5);
    const char* str2 = csv_string_pool_intern(pool, "world", 5);
    const char* str3 = csv_string_pool_intern(pool, "hello", 5);
    
    ASSERT(str1 != NULL, "String interning failed");
    ASSERT(str2 != NULL, "String interning failed");
    ASSERT(str3 != NULL, "String interning failed");
    ASSERT(str1 == str3, "String interning deduplication failed");
    ASSERT_STR_EQ("hello", str1, 5, "String content mismatch");
    ASSERT_STR_EQ("world", str2, 5, "String content mismatch");
    
    csv_string_pool_destroy(pool);
    return true;
}

bool test_error_handling(void) {
    csv_parser_t* parser = csv_parser_create(NULL);
    ASSERT(parser != NULL, "Failed to create parser");
    
    csv_error_t err = csv_parse_buffer(parser, NULL, 0, true);
    ASSERT_EQ(CSV_ERROR_INVALID_ARGS, err, "Expected invalid args error");
    
    err = csv_parse_buffer(NULL, CSV_SIMPLE, strlen(CSV_SIMPLE), true);
    ASSERT_EQ(CSV_ERROR_INVALID_ARGS, err, "Expected invalid args error");
    
    err = csv_parse_file(parser, "nonexistent_file_12345.csv");
    ASSERT_EQ(CSV_ERROR_IO_ERROR, err, "Expected IO error");
    
    csv_parser_destroy(parser);
    return true;
}

bool test_performance_comparison(void) {
    const size_t NUM_ROWS = 1000;
    size_t data_size;
    
    char* test_data = malloc(NUM_ROWS * 50);
    ASSERT(test_data != NULL, "Failed to allocate test data");
    
    strcpy(test_data, "id,name,email\n");
    char* pos = test_data + strlen("id,name,email\n");
    
    for (size_t i = 0; i < NUM_ROWS; i++) {
        int written = sprintf(pos, "%zu,User%zu,user%zu@test.com\n", i, i, i);
        pos += written;
    }
    data_size = pos - test_data;
    
    test_callback_data_t data = {0};
    
    csv_simd_init();
    csv_parser_t* parser = csv_parser_create(NULL);
    csv_parser_set_row_callback(parser, test_row_callback, &data);
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    csv_error_t err = csv_parse_buffer(parser, test_data, data_size, true);
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double throughput = (data_size / (1024.0 * 1024.0)) / elapsed;
    
    printf("\n  Performance: %.2f MB/s (%.3f ms for %d rows)\n  ", 
           throughput, elapsed * 1000, data.row_count);
    
    ASSERT_EQ(CSV_OK, err, "Performance test parse error");
    ASSERT_EQ((int)(NUM_ROWS + 1), data.row_count, "Wrong row count in performance test");
    
    csv_parser_destroy(parser);
    free(test_data);
    return true;
}

void run_benchmark(const char* name, const char* data, size_t size, int iterations) {
    printf("\nBenchmark: %s\n", name);
    
    double total_time = 0;
    size_t total_rows = 0;
    
    for (int i = 0; i < iterations; i++) {
        test_callback_data_t callback_data = {0};
        csv_parser_t* parser = csv_parser_create(NULL);
        csv_parser_set_row_callback(parser, test_row_callback, &callback_data);
        
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        csv_parse_buffer(parser, data, size, true);
        
        clock_gettime(CLOCK_MONOTONIC, &end);
        
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
        total_time += elapsed;
        total_rows = callback_data.row_count;
        
        csv_parser_destroy(parser);
    }
    
    double avg_time = total_time / iterations;
    double throughput = (size / (1024.0 * 1024.0)) / avg_time;
    
    printf("  Average time: %.3f ms\n", avg_time * 1000);
    printf("  Throughput: %.2f MB/s\n", throughput);
    printf("  Rows processed: %zu\n", total_rows);
}

int main(void) {
    printf("SonicCSV Extended Test Suite\n");
    printf("============================\n\n");
    
    csv_simd_init();
    
    TEST(simd_feature_detection);
    TEST(basic_parsing);
    TEST(quoted_fields);
    TEST(mixed_quoting);
    TEST(empty_fields);
    TEST(unicode_support);
    TEST(tsv_parsing);
    TEST(crlf_handling);
    TEST(escaped_quotes);
    TEST(mode_detection);
    TEST(simd_operations);
    TEST(block_parser);
    TEST(large_file_simulation);
    TEST(file_operations);
    TEST(string_pool);
    TEST(error_handling);
    TEST(performance_comparison);
    
    printf("\n");
    printf("Test Results\n");
    printf("============\n");
    printf("Total tests: %d\n", total_tests);
    printf("Passed: %d\n", passed_tests);
    printf("Failed: %d\n", failed_tests);
    
    if (failed_tests == 0) {
        printf("\n🎉 All tests passed!\n");
        
        printf("\nRunning benchmarks...\n");
        run_benchmark("Simple CSV", CSV_SIMPLE, strlen(CSV_SIMPLE), 1000);
        run_benchmark("Quoted CSV", CSV_QUOTED, strlen(CSV_QUOTED), 1000);
        run_benchmark("TSV Data", TSV_DATA, strlen(TSV_DATA), 1000);
        
    } else {
        printf("\n❌ Some tests failed!\n");
        return 1;
    }
    
    return 0;
}