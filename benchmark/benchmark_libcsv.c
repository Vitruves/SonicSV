#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <csv.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <csv_file>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];

    // Initialize libcsv parser
    struct csv_parser parser;
    if (csv_init(&parser, 0) != 0) {
        fprintf(stderr, "Error: Could not initialize parser\n");
        return 1;
    }

    // Open the file
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Could not open file %s\n", filename);
        csv_free(&parser);
        return 1;
    }

    // Measure parsing time
    clock_t start = clock();
    char buffer[4096];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (csv_parse(&parser, buffer, bytes_read, NULL, NULL, NULL) != bytes_read) {
            fprintf(stderr, "Error parsing file: %s\n", csv_strerror(csv_error(&parser)));
            fclose(file);
            csv_free(&parser);
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

    // Clean up
    csv_free(&parser);
    return 0;
}
