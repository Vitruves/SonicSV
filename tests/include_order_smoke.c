/*
 * Include-order smoke test.
 *
 * Standard library headers are deliberately included BEFORE sonicsv.h. On
 * glibc, this is the order under which `_POSIX_C_SOURCE` / `_DEFAULT_SOURCE`
 * defines inside sonicsv.h are too late — <features.h> has already locked
 * in the visible POSIX surface. The header-local fallback defines wouldn't
 * help here; only -D flags on the compiler invocation will.
 *
 * If this TU compiles cleanly, the project's build flags (passed via
 * Makefile CFLAGS) are correctly defining the POSIX feature macros up
 * front, so users who copy sonicsv.h into a project alongside other
 * headers won't run into the trap.
 *
 * The body just exercises the public API enough to force the
 * implementation's POSIX-dependent functions (clock_gettime, posix_madvise,
 * mmap) to be referenced.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* This TU intentionally defines SONICSV_IMPLEMENTATION itself — the
 * point of the smoke test is the in-source-define path. Guard against
 * redefinition since the project's Makefile also passes -D...IMPLEMENTATION
 * via CFLAGS. */
#ifndef SONICSV_IMPLEMENTATION
#define SONICSV_IMPLEMENTATION
#endif
#include "../sonicsv.h"

static int g_rows = 0;
static void on_row(const csv_row_t *r, void *u) { (void)r; (void)u; g_rows++; }

int main(void) {
    csv_parser_t *p = csv_parser_create(NULL);
    if (!p) { fprintf(stderr, "create failed\n"); return 1; }
    csv_parser_set_row_callback(p, on_row, NULL);
    csv_error_t err = csv_parse_string(p, "a,b,c\n1,2,3\n");
    csv_parser_destroy(p);
    if (err != CSV_OK || g_rows != 2) {
        fprintf(stderr, "include-order smoke FAIL: err=%d rows=%d\n", err, g_rows);
        return 1;
    }
    return 0;
}
