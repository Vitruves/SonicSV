#define SONICSV_IMPLEMENTATION
#include "../sonicsv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <csv_file>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];

    csv_parser_t *parser = csv_parser_create(NULL);
    if (!parser) {
        fprintf(stderr, "Error: Could not create parser\n");
        return 1;
    }

    // Open the file
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Could not open file %s\n", filename);
        csv_parser_destroy(parser);
        return 1;
    }

    // Measure parsing time
    clock_t start = clock();
    char buffer[4096];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        csv_error_t err = csv_parse_buffer(parser, buffer, bytes_read, feof(file));
        if (err != CSV_OK) {
            fprintf(stderr, "Error parsing file: %s\n", csv_error_string(err));
            fclose(file);
            csv_parser_destroy(parser);
            return 1;
        }
    }
    clock_t end = clock();

    // Calculate throughput
    double elapsed_time = (double)(end - start) / CLOCKS_PER_SEC;
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fclose(file);
    double throughput_mbps = (file_size / (1024.0 * 1024.0)) / elapsed_time;

    printf("%.2f\n", throughput_mbps);

    csv_parser_destroy(parser);
    return 0;
} 
