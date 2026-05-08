// C++ smoke test: confirms sonicsv.h is usable from a C++ translation unit.
// Covers the core public API path; not a full functional suite (see
// sonicsv_test.c for that).
//
// This TU intentionally does NOT define SONICSV_IMPLEMENTATION. The
// implementation is compiled separately from sonicsv_impl.c and linked in,
// which mirrors the realistic C++ usage pattern (single-header lib's impl
// goes in one .c TU; .cpp TUs see only the extern "C" public API).

// Make sure the public header parses cleanly even if SONICSV_IMPLEMENTATION
// happens to be set globally (e.g. via -D in the build system).
#ifdef SONICSV_IMPLEMENTATION
#  undef SONICSV_IMPLEMENTATION
#endif

#include "../sonicsv.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int g_failures = 0;

#define CPP_CHECK(cond, msg)                                                   \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__,    \
                         (msg));                                               \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

struct CollectedRow {
    std::vector<std::string> fields;
    uint64_t row_number;
};

struct Collector {
    std::vector<CollectedRow> rows;
    int errors = 0;
};

static void collect_row(const csv_row_t *row, void *user_data) {
    Collector *c = static_cast<Collector *>(user_data);
    CollectedRow r;
    r.row_number = row->row_number;
    for (size_t i = 0; i < row->num_fields; ++i) {
        const csv_field_t *f = csv_get_field(row, i);
        r.fields.emplace_back(f->data, f->size);
    }
    c->rows.push_back(std::move(r));
}

static void collect_error(csv_error_t, const char *, uint64_t, void *user_data) {
    static_cast<Collector *>(user_data)->errors++;
}

static bool test_create_destroy() {
    csv_parser_t *p = csv_parser_create(nullptr);
    CPP_CHECK(p != nullptr, "create with default options");
    csv_parser_destroy(p);
    return true;
}

static bool test_parse_string_basic() {
    Collector c;
    csv_parser_t *p = csv_parser_create(nullptr);
    csv_parser_set_row_callback(p, collect_row, &c);
    csv_parser_set_error_callback(p, collect_error, &c);

    csv_error_t err = csv_parse_string(p, "a,b,c\n1,2,3\n");
    CPP_CHECK(err == CSV_OK, "parse_string returns OK");
    CPP_CHECK(c.errors == 0, "no errors raised");
    CPP_CHECK(c.rows.size() == 2, "two rows parsed");
    CPP_CHECK(c.rows[0].fields == (std::vector<std::string>{"a", "b", "c"}), "header row");
    CPP_CHECK(c.rows[1].fields == (std::vector<std::string>{"1", "2", "3"}), "data row");

    csv_parser_destroy(p);
    return true;
}

static bool test_parse_buffer_quoted() {
    Collector c;
    csv_parser_t *p = csv_parser_create(nullptr);
    csv_parser_set_row_callback(p, collect_row, &c);

    const char *csv = "name,note\n\"Doe, J\",\"line1\nline2\"\n";
    csv_error_t err = csv_parse_buffer(p, csv, std::strlen(csv), true);
    CPP_CHECK(err == CSV_OK, "parse_buffer returns OK");
    CPP_CHECK(c.rows.size() == 2, "header + one quoted row");
    CPP_CHECK(c.rows[1].fields.size() == 2, "two fields in quoted row");
    CPP_CHECK(c.rows[1].fields[0] == "Doe, J", "quoted field with comma");
    CPP_CHECK(c.rows[1].fields[1] == "line1\nline2", "quoted field with newline");

    csv_parser_destroy(p);
    return true;
}

static bool test_custom_options() {
    csv_parse_options_t opts = csv_default_options();
    opts.delimiter = '\t';
    opts.trim_whitespace = true;

    Collector c;
    csv_parser_t *p = csv_parser_create(&opts);
    csv_parser_set_row_callback(p, collect_row, &c);

    csv_error_t err = csv_parse_string(p, "  alpha\t  beta  \tgamma\n");
    CPP_CHECK(err == CSV_OK, "tab-delimited parse OK");
    CPP_CHECK(c.rows.size() == 1, "one row");
    CPP_CHECK(c.rows[0].fields.size() == 3, "three fields");
    CPP_CHECK(c.rows[0].fields[0] == "alpha", "trim left");
    CPP_CHECK(c.rows[0].fields[1] == "beta", "trim both sides");
    CPP_CHECK(c.rows[0].fields[2] == "gamma", "no trim needed");

    csv_parser_destroy(p);
    return true;
}

static bool test_stats_and_simd_features() {
    csv_parser_t *p = csv_parser_create(nullptr);
    Collector c;
    csv_parser_set_row_callback(p, collect_row, &c);
    csv_parse_string(p, "x,y\n1,2\n3,4\n");

    csv_stats_t stats = csv_parser_get_stats(p);
    CPP_CHECK(stats.total_rows_parsed == 3, "stats report 3 rows");
    CPP_CHECK(stats.total_fields_parsed == 6, "stats report 6 fields");

    uint32_t simd = csv_get_simd_features();
    (void)simd;  // value depends on host CPU; just check the call links

    const char *msg = csv_error_string(CSV_OK);
    CPP_CHECK(msg != nullptr && msg[0] != '\0', "error_string returns non-empty");

    csv_parser_destroy(p);
    return true;
}

int main() {
    test_create_destroy();
    test_parse_string_basic();
    test_parse_buffer_quoted();
    test_custom_options();
    test_stats_and_simd_features();

    if (g_failures == 0) {
        return 0;
    }
    std::fprintf(stderr, "C++ smoke tests: %d failure(s)\n", g_failures);
    return 1;
}
