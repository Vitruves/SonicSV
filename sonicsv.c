/**
 * SonicSV - Fast, portable CSV streaming library
 * 
 * GitHub: https://github.com/Vitruves/SonicSV
 * 
 * Copyright (c) 2023 SonicSV Contributors
 * MIT License
 */

#include "sonicsv.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Error buffer */
static char error_buffer[256] = {0};

/* Configuration constants */
#define SONICSV_DEFAULT_BUFFER_SIZE (64 * 1024)
#define SONICSV_DEFAULT_FIELD_CAPACITY 32
#define SONICSV_MIN_BUFFER_SIZE (4 * 1024)
#define SONICSV_MAX_BUFFER_SIZE (16 * 1024 * 1024)

/* Row structure definition */
struct SonicSVRow {
    char** fields;
    size_t* field_lengths;
    size_t field_count;
    size_t capacity;
};

/* Stream structure definition */
struct SonicSV {
    FILE* file;
    SonicSVConfig config;
    char* buffer;
    size_t buffer_size;
    size_t buffer_pos;
    size_t bytes_in_buffer;
    bool in_quote;
    char* line_buffer;
    size_t line_capacity;
    size_t line_length;
    bool eof_reached;
};

/* Forward declarations */
static void set_error(const char* format, ...);
static bool ensure_row_capacity(SonicSVRow* row, size_t capacity);
static bool ensure_line_capacity(SonicSV* stream, size_t needed);
static bool process_field(SonicSVRow* row, const char* field, size_t length, bool quoted);

SonicSVConfig sonicsv_default_config(void) {
    SonicSVConfig config;
    config.delimiter = ',';
    config.quote_char = '"';
    config.trim_whitespace = false;
    config.skip_empty_lines = true;
    config.buffer_size = SONICSV_DEFAULT_BUFFER_SIZE;
    return config;
}

void set_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(error_buffer, sizeof(error_buffer), format, args);
    va_end(args);
}

const char* sonicsv_get_error(void) {
    return error_buffer[0] ? error_buffer : NULL;
}

SonicSV* sonicsv_open(const char* filename, const SonicSVConfig* config) {
    /* Validate inputs */
    if (!filename) {
        set_error("Filename cannot be NULL");
        return NULL;
    }
    
    /* Create stream object */
    SonicSV* stream = (SonicSV*)calloc(1, sizeof(SonicSV));
    if (!stream) {
        set_error("Memory allocation failed for stream object");
        return NULL;
    }
    
    /* Use default config if not provided */
    if (config) {
        stream->config = *config;
    } else {
        stream->config = sonicsv_default_config();
    }
    
    /* Validate and adjust buffer size */
    if (stream->config.buffer_size < SONICSV_MIN_BUFFER_SIZE) {
        stream->config.buffer_size = SONICSV_MIN_BUFFER_SIZE;
    } else if (stream->config.buffer_size > SONICSV_MAX_BUFFER_SIZE) {
        stream->config.buffer_size = SONICSV_MAX_BUFFER_SIZE;
    }
    
    /* Open the file */
    stream->file = fopen(filename, "r");
    if (!stream->file) {
        set_error("Failed to open file '%s': %s", filename, strerror(errno));
        free(stream);
        return NULL;
    }
    
    /* Allocate read buffer */
    stream->buffer = (char*)malloc(stream->config.buffer_size);
    if (!stream->buffer) {
        set_error("Memory allocation failed for read buffer");
        fclose(stream->file);
        free(stream);
        return NULL;
    }
    
    /* Allocate initial line buffer */
    stream->line_capacity = stream->config.buffer_size / 2;
    stream->line_buffer = (char*)malloc(stream->line_capacity);
    if (!stream->line_buffer) {
        set_error("Memory allocation failed for line buffer");
        free(stream->buffer);
        fclose(stream->file);
        free(stream);
        return NULL;
    }
    
    /* Initialize remaining fields */
    stream->buffer_pos = 0;
    stream->bytes_in_buffer = 0;
    stream->in_quote = false;
    stream->line_length = 0;
    stream->eof_reached = false;
    
    return stream;
}

void sonicsv_close(SonicSV* stream) {
    if (!stream) return;
    
    if (stream->file) {
        fclose(stream->file);
    }
    
    free(stream->buffer);
    free(stream->line_buffer);
    free(stream);
}

static bool ensure_line_capacity(SonicSV* stream, size_t needed) {
    if (stream->line_length + needed <= stream->line_capacity) {
        return true;
    }
    
    /* Calculate new capacity, doubling until it's enough */
    size_t new_capacity = stream->line_capacity;
    while (new_capacity < stream->line_length + needed) {
        new_capacity *= 2;
        
        /* Check for overflow */
        if (new_capacity < stream->line_capacity) {
            set_error("Line buffer overflow");
            return false;
        }
    }
    
    /* Reallocate buffer */
    char* new_buffer = (char*)realloc(stream->line_buffer, new_capacity);
    if (!new_buffer) {
        set_error("Failed to resize line buffer");
        return false;
    }
    
    stream->line_buffer = new_buffer;
    stream->line_capacity = new_capacity;
    return true;
}

static bool read_more_data(SonicSV* stream) {
    if (stream->eof_reached) {
        return false;
    }
    
    /* If there's unused data, move it to the beginning */
    if (stream->buffer_pos > 0 && stream->buffer_pos < stream->bytes_in_buffer) {
        size_t remaining = stream->bytes_in_buffer - stream->buffer_pos;
        memmove(stream->buffer, stream->buffer + stream->buffer_pos, remaining);
        stream->bytes_in_buffer = remaining;
    } else {
        stream->bytes_in_buffer = 0;
    }
    
    stream->buffer_pos = 0;
    
    /* Read more data */
    size_t bytes_read = fread(
        stream->buffer + stream->bytes_in_buffer,
        1,
        stream->config.buffer_size - stream->bytes_in_buffer,
        stream->file
    );
    
    stream->bytes_in_buffer += bytes_read;
    
    if (bytes_read == 0) {
        stream->eof_reached = true;
        return stream->bytes_in_buffer > 0;  /* Return true if we still have data */
    }
    
    return true;
}

SonicSVRow* sonicsv_next_row(SonicSV* stream) {
    if (!stream) {
        set_error("Stream cannot be NULL");
        return NULL;
    }
    
    /* Reset line buffer */
    stream->line_length = 0;
    stream->in_quote = false;
    
    /* Read data until we have a complete line */
    bool line_complete = false;
    bool have_data = (stream->buffer_pos < stream->bytes_in_buffer);
    
    while (!line_complete) {
        /* Read more data if needed */
        if (!have_data) {
            have_data = read_more_data(stream);
            if (!have_data) {
                /* End of file with no pending data */
                if (stream->line_length == 0) {
                    return NULL;
                }
                
                /* Treat EOF as end of line */
                line_complete = true;
                break;
            }
        }
        
        /* Process current buffer */
        while (stream->buffer_pos < stream->bytes_in_buffer) {
            char c = stream->buffer[stream->buffer_pos++];
            
            /* Handle quotes */
            if (c == stream->config.quote_char) {
                stream->in_quote = !stream->in_quote;
            }
            
            /* Check for end of line when not in quotes */
            if (c == '\n' && !stream->in_quote) {
                line_complete = true;
                break;
            }
            
            /* Add character to line buffer */
            if (!ensure_line_capacity(stream, 1)) {
                return NULL;  /* Error set by ensure_line_capacity */
            }
            
            stream->line_buffer[stream->line_length++] = c;
        }
        
        have_data = false;  /* Need more data for next iteration */
    }
    
    /* Skip empty lines if configured */
    if (stream->config.skip_empty_lines && stream->line_length == 0) {
        return sonicsv_next_row(stream);
    }
    
    /* Terminate line buffer */
    if (!ensure_line_capacity(stream, 1)) {
        return NULL;
    }
    stream->line_buffer[stream->line_length] = '\0';
    
    /* Trim trailing CR if present */
    if (stream->line_length > 0 && stream->line_buffer[stream->line_length - 1] == '\r') {
        stream->line_buffer[--stream->line_length] = '\0';
    }
    
    /* Parse fields */
    SonicSVRow* row = (SonicSVRow*)calloc(1, sizeof(SonicSVRow));
    if (!row) {
        set_error("Memory allocation failed for row");
        return NULL;
    }
    
    row->capacity = SONICSV_DEFAULT_FIELD_CAPACITY;
    row->fields = (char**)malloc(row->capacity * sizeof(char*));
    row->field_lengths = (size_t*)malloc(row->capacity * sizeof(size_t));
    
    if (!row->fields || !row->field_lengths) {
        set_error("Memory allocation failed for row fields");
        free(row->fields);
        free(row->field_lengths);
        free(row);
        return NULL;
    }
    
    /* Split into fields */
    const char* line = stream->line_buffer;
    const char* field_start = line;
    bool in_quotes = false;
    
    for (size_t i = 0; i <= stream->line_length; i++) {
        char c = (i < stream->line_length) ? line[i] : '\0';
        
        if (c == stream->config.quote_char) {
            /* Handle escaped quotes */
            if (in_quotes && i + 1 < stream->line_length && line[i + 1] == stream->config.quote_char) {
                i++;  /* Skip the next quote */
            } else {
                in_quotes = !in_quotes;
            }
        } else if ((c == stream->config.delimiter || c == '\0') && !in_quotes) {
            /* Found field delimiter or end of line */
            size_t field_len = &line[i] - field_start;
            bool is_quoted = false;
            
            /* Check if field is quoted */
            if (field_len >= 2 && field_start[0] == stream->config.quote_char && 
                field_start[field_len - 1] == stream->config.quote_char) {
                is_quoted = true;
                field_start++;  /* Skip opening quote */
                field_len -= 2;  /* Adjust for quotes */
            }
            
            /* Add field to row */
            if (!process_field(row, field_start, field_len, is_quoted)) {
                sonicsv_row_free(row);
                return NULL;
            }
            
            field_start = &line[i + 1];  /* Next field starts after delimiter */
        }
    }
    
    return row;
}

static bool process_field(SonicSVRow* row, const char* field, size_t length, bool quoted) {
    /* Ensure row has capacity for one more field */
    if (!ensure_row_capacity(row, row->field_count + 1)) {
        return false;
    }
    
    /* Allocate space for field data */
    char* field_data = (char*)malloc(length + 1);
    if (!field_data) {
        set_error("Memory allocation failed for field data");
        return false;
    }
    
    /* Copy field data, unescaping quotes if needed */
    size_t pos = 0;
    
    if (quoted) {
        for (size_t i = 0; i < length; i++) {
            if (field[i] == '"' && i + 1 < length && field[i + 1] == '"') {
                field_data[pos++] = '"';
                i++;  /* Skip the second quote */
            } else {
                field_data[pos++] = field[i];
            }
        }
    } else {
        memcpy(field_data, field, length);
        pos = length;
    }
    
    field_data[pos] = '\0';
    
    /* Add field to row */
    row->fields[row->field_count] = field_data;
    row->field_lengths[row->field_count] = pos;
    row->field_count++;
    
    return true;
}

static bool ensure_row_capacity(SonicSVRow* row, size_t capacity) {
    if (capacity <= row->capacity) {
        return true;
    }
    
    size_t new_capacity = row->capacity * 2;
    while (new_capacity < capacity) {
        new_capacity *= 2;
    }
    
    char** new_fields = (char**)realloc(row->fields, new_capacity * sizeof(char*));
    size_t* new_lengths = (size_t*)realloc(row->field_lengths, new_capacity * sizeof(size_t));
    
    if (!new_fields || !new_lengths) {
        free(new_fields);  /* Might be NULL, but free handles that */
        free(new_lengths);
        set_error("Failed to resize row capacity");
        return false;
    }
    
    row->fields = new_fields;
    row->field_lengths = new_lengths;
    row->capacity = new_capacity;
    
    return true;
}

void sonicsv_row_free(SonicSVRow* row) {
    if (!row) return;
    
    if (row->fields) {
        for (size_t i = 0; i < row->field_count; i++) {
            free(row->fields[i]);
        }
        free(row->fields);
    }
    
    free(row->field_lengths);
    free(row);
}

size_t sonicsv_row_get_field_count(const SonicSVRow* row) {
    return row ? row->field_count : 0;
}

const char* sonicsv_row_get_field(const SonicSVRow* row, size_t index) {
    if (!row || index >= row->field_count) {
        return NULL;
    }
    
    return row->fields[index];
} 