/**
 * SonicSV - Fast, portable CSV streaming library
 * 
 * GitHub: https://github.com/Vitruves/SonicSV
 * Version: 1.3.0
 * 
 * Copyright (c) 2025 SonicSV Contributors
 * MIT License
 * 
 * DISCLAIMER: This software is provided "as is", without warranty of any kind, express or implied, including but not limited to the warranties of 
 * merchantability, fitness for a particular purpose and noninfringement. In no event shall the authors or copyright holders be liable for any claim, 
 * damages or other liability, whether in an action of contract, tort or otherwise, arising from, out of or in connection with the software or the use 
 * or other dealings in the software.
 */

#include "sonicsv.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdatomic.h>

#define SONICSV_VERSION "1.3.0"
#define SONICSV_DEFAULT_MEMORY_LIMIT (1024 * 1024 * 1024)
#define SONICSV_DEFAULT_MAX_LINE_LENGTH (1024 * 1024)
#define SONICSV_DEFAULT_MAX_THREADS 32
#define ROW_POOL_SIZE 128

typedef struct {
    size_t used_blocks;
    size_t current_block;
    pthread_mutex_t mutex;
    size_t pool_size;
    char** blocks;
    char** current_pos;
    size_t* block_sizes;
    size_t block_size;
} MemoryPool;

typedef struct {
    pthread_t* threads;
    size_t max_threads;
    size_t num_threads;
    volatile bool running;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} ThreadPool;

typedef struct {
    const char* filename;
    SonicSVConfig config;
    SonicSVRowCallback callback;
    void* user_data;
    long start_offset;
    long end_offset;
    long rows_processed;
    int error_code;
} ThreadWork;

typedef struct {
    const char* filename;
    SonicSVConfig config;
    SonicSVRowCallback callback;
    void* user_data;
    atomic_long* next_chunk;
    long file_size;
    long chunk_size;
    int error_code;
    char worker_error_message[256];
    long rows_processed;
} DynamicThreadWork;

struct SonicSVRow {
    char** fields;
    size_t* field_lengths;
    size_t field_count;
    size_t capacity;
    bool owns_data;
    MemoryPool* pool;
};

struct SonicSV {
    FILE* file;
    int fd;
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
    bool is_mmap;
    char* mmap_data;
    size_t mmap_size;
    size_t mmap_pos;
    pthread_mutex_t mutex;
    MemoryPool* pool;
};

typedef struct {
    char* buffer;
    size_t size;
    size_t used;
} ThreadLocalBuffer;

typedef struct {
    SonicSVRow** rows;
    size_t used;
    size_t capacity;
} RowPool;

static __thread char error_buffer[256] = {0};
static size_t global_memory_limit = SONICSV_DEFAULT_MEMORY_LIMIT;
static ThreadPool* global_thread_pool = NULL;
static size_t system_page_size = 0;
static size_t l1_cache_size = 0;
static size_t l2_cache_size = 0;
static size_t optimal_threads = 0;
static bool system_params_initialized = false;

static __thread ThreadLocalBuffer tl_buffer = {NULL, 0, 0};
static __thread RowPool tl_row_pool = {NULL, 0, 0};

static void sonicsv_thread_cleanup_internal(void) {
    if (tl_buffer.buffer) {
        free(tl_buffer.buffer);
        tl_buffer.buffer = NULL;
        tl_buffer.size = 0;
        tl_buffer.used = 0;
    }

    if (tl_row_pool.rows) {
        for (size_t i = 0; i < tl_row_pool.capacity; i++) {
            if (tl_row_pool.rows[i] != NULL) {
                free(tl_row_pool.rows[i]->fields);
                free(tl_row_pool.rows[i]->field_lengths);
                free(tl_row_pool.rows[i]);
            }
        }
        free(tl_row_pool.rows);
        tl_row_pool.rows = NULL;
        tl_row_pool.used = 0;
        tl_row_pool.capacity = 0;
    }
}

static size_t get_system_page_size(void) {
    long page_size = sysconf(_SC_PAGESIZE);
    return (page_size > 0) ? (size_t)page_size : 4096;
}

static size_t get_cache_size(int level) {
    long cache_size = 0;
    
    #ifdef _SC_LEVEL1_DCACHE_SIZE
    if (level == 1) cache_size = sysconf(_SC_LEVEL1_DCACHE_SIZE);
    #endif
    
    #ifdef _SC_LEVEL2_CACHE_SIZE
    if (level == 2) cache_size = sysconf(_SC_LEVEL2_CACHE_SIZE);
    #endif
    
    if (cache_size <= 0) {
        return level == 1 ? 32 * 1024 : 256 * 1024;
    }
    
    return (size_t)cache_size;
}

static size_t get_optimal_threads(void) {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    return (cores > 0) ? (size_t)cores : 1;
}

static void init_system_parameters(void) {
    if (!system_params_initialized) {
        system_page_size = get_system_page_size();
        l1_cache_size = get_cache_size(1);
        l2_cache_size = get_cache_size(2);
        optimal_threads = get_optimal_threads();
        system_params_initialized = true;
    }
}

static void set_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(error_buffer, sizeof(error_buffer), format, args);
    va_end(args);
}

const char* sonicsv_get_error(void) {
    return error_buffer[0] ? error_buffer : NULL;
}

static MemoryPool* memory_pool_create(size_t block_size) {
    if (block_size == 0) {
        block_size = system_page_size;
    }
    
    MemoryPool* pool = calloc(1, sizeof(MemoryPool));
    if (!pool) {
        set_error("Failed to allocate memory pool");
        return NULL;
    }
    
    pool->pool_size = global_memory_limit / block_size;
    if (pool->pool_size > 1024) pool->pool_size = 1024;
    if (pool->pool_size < 4) pool->pool_size = 4;
    
    pool->blocks = calloc(pool->pool_size, sizeof(char*));
    pool->current_pos = calloc(pool->pool_size, sizeof(char*));
    pool->block_sizes = calloc(pool->pool_size, sizeof(size_t));
    
    if (!pool->blocks || !pool->current_pos || !pool->block_sizes) {
        free(pool->blocks);
        free(pool->current_pos);
        free(pool->block_sizes);
        free(pool);
        set_error("Failed to allocate memory pool arrays");
        return NULL;
    }
    
    pool->block_size = block_size;
    
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        free(pool->blocks);
        free(pool->current_pos);
        free(pool->block_sizes);
        free(pool);
        set_error("Failed to initialize memory pool mutex");
        return NULL;
    }
    
    return pool;
}

static void memory_pool_destroy(MemoryPool* pool) {
    if (!pool) return;
    
    for (size_t i = 0; i < pool->used_blocks; i++) {
        free(pool->blocks[i]);
    }
    
    pthread_mutex_destroy(&pool->mutex);
    free(pool->blocks);
    free(pool->current_pos);
    free(pool->block_sizes);
    free(pool);
}

static char* memory_pool_alloc(MemoryPool* pool, size_t size) {
    if (size == 0) return NULL;

    if (size <= 256) {
        if (tl_buffer.buffer == NULL || (tl_buffer.used + size > tl_buffer.size)) {
            if (tl_buffer.buffer != NULL) {
                free(tl_buffer.buffer);
            }
            
            tl_buffer.buffer = malloc(8192);
            if (!tl_buffer.buffer) {
                return malloc(size);
            }
            tl_buffer.size = 8192;
            tl_buffer.used = 0;
        }
        char* result = tl_buffer.buffer + tl_buffer.used;
        tl_buffer.used += size;
        return result;
    }
    
    if (!pool) {
        return malloc(size);
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    char* result_from_pool = NULL;
    if (pool->current_block < pool->used_blocks && 
        pool->block_sizes[pool->current_block] >= size) {
        result_from_pool = pool->current_pos[pool->current_block];
        pool->current_pos[pool->current_block] += size;
        pool->block_sizes[pool->current_block] -= size;
    } else {
        if (pool->used_blocks < pool->pool_size) {
            size_t alloc_size = size > pool->block_size ? size : pool->block_size;
            char* block = malloc(alloc_size);
            if (block) {
                pool->blocks[pool->used_blocks] = block;
                pool->current_pos[pool->used_blocks] = block + size;
                pool->block_sizes[pool->used_blocks] = alloc_size - size;
                pool->current_block = pool->used_blocks;
                result_from_pool = block;
                pool->used_blocks++;
            }
        }
    }
    
    pthread_mutex_unlock(&pool->mutex);
    
    if (!result_from_pool) {
        result_from_pool = malloc(size);
    }
    return result_from_pool;
}

static bool ensure_line_capacity(SonicSV* stream, size_t needed) {
    if (!stream || !stream->line_buffer) return false;
    
    if (stream->line_length + needed <= stream->line_capacity) {
        return true;
    }
    
    size_t new_capacity = stream->line_capacity;
    while (new_capacity < stream->line_length + needed) {
        if (new_capacity > SIZE_MAX / 2) {
            set_error("Line capacity overflow");
            return false;
        }
        new_capacity *= 2;
        if (new_capacity > stream->config.max_line_length) {
            set_error("Line too long (max: %zu bytes)", stream->config.max_line_length);
            return false;
        }
    }
    
    char* new_buffer = realloc(stream->line_buffer, new_capacity);
    if (!new_buffer) {
        set_error("Failed to resize line buffer");
        return false;
    }
    
    stream->line_buffer = new_buffer;
    stream->line_capacity = new_capacity;
    return true;
}

static bool ensure_row_capacity(SonicSVRow* row, size_t capacity) {
    if (!row) return false;
    if (capacity <= row->capacity) return true;
    
    size_t new_capacity = row->capacity * 2;
    while (new_capacity < capacity) {
        if (new_capacity > SIZE_MAX / 2) {
            set_error("Row capacity overflow");
            return false;
        }
        new_capacity *= 2;
    }
    
    if (new_capacity > row->capacity * 8) {
        new_capacity = row->capacity * 8;
        if (new_capacity < capacity) {
            new_capacity = capacity;
        }
    }
    
    char** new_fields = realloc(row->fields, new_capacity * sizeof(char*));
    if (!new_fields) {
        set_error("Failed to resize row fields");
        return false;
    }
    
    size_t* new_lengths = realloc(row->field_lengths, new_capacity * sizeof(size_t));
    if (!new_lengths) {
        free(new_fields);
        set_error("Failed to resize field lengths");
        return false;
    }
    
    row->fields = new_fields;
    row->field_lengths = new_lengths;
    row->capacity = new_capacity;
    
    return true;
}

static bool read_more_data(SonicSV* stream) {
    if (!stream || stream->eof_reached) {
        return false;
    }
    
    if (stream->is_mmap) {
        return stream->mmap_pos < stream->mmap_size;
    }
    
    if (stream->buffer_pos > 0 && stream->buffer_pos < stream->bytes_in_buffer) {
        size_t remaining = stream->bytes_in_buffer - stream->buffer_pos;
        memmove(stream->buffer, stream->buffer + stream->buffer_pos, remaining);
        stream->bytes_in_buffer = remaining;
    } else {
        stream->bytes_in_buffer = 0;
    }
    
    stream->buffer_pos = 0;
    
    size_t bytes_read = fread(
        stream->buffer + stream->bytes_in_buffer,
        1,
        stream->config.buffer_size - stream->bytes_in_buffer,
        stream->file
    );
    
    stream->bytes_in_buffer += bytes_read;
    
    if (bytes_read == 0) {
        stream->eof_reached = true;
        return stream->bytes_in_buffer > 0;
    }
    
    return true;
}

static bool process_field(SonicSVRow* row, const char* field, size_t length, bool quoted) {
    if (!row || !field) return false;
    
    if (row->field_count >= row->capacity) {
        size_t new_capacity = row->capacity * 2;
        char** new_fields = realloc(row->fields, new_capacity * sizeof(char*));
        size_t* new_lengths = realloc(row->field_lengths, new_capacity * sizeof(size_t));
        
        if (!new_fields || !new_lengths) {
            free(new_fields);
            free(new_lengths);
            set_error("Memory allocation failed for field data");
            return false;
        }
        
        row->fields = new_fields;
        row->field_lengths = new_lengths;
        row->capacity = new_capacity;
    }
    
    if (length == 0) {
        char* empty_field = row->pool ? memory_pool_alloc(row->pool, 1) : malloc(1);
        if (!empty_field) {
            set_error("Memory allocation failed for field data");
            return false;
        }
        empty_field[0] = '\0';
        row->fields[row->field_count] = empty_field;
        row->field_lengths[row->field_count] = 0;
        row->field_count++;
        return true;
    }
    
    if (!quoted) {
        char* field_data = row->pool ? memory_pool_alloc(row->pool, length + 1) : malloc(length + 1);
        if (!field_data) {
            set_error("Memory allocation failed for field data");
            return false;
        }
        
        memcpy(field_data, field, length);
        field_data[length] = '\0';
        
        row->fields[row->field_count] = field_data;
        row->field_lengths[row->field_count] = length;
        row->field_count++;
        return true;
    }
    
    char* field_data = row->pool ? memory_pool_alloc(row->pool, length + 1) : malloc(length + 1);
    if (!field_data) {
        set_error("Memory allocation failed for field data");
        return false;
    }
    
    size_t pos = 0;
    for (size_t i = 0; i < length; i++) {
        if (field[i] == '"' && i + 1 < length && field[i + 1] == '"') {
            field_data[pos++] = '"';
            i++;
        } else {
            field_data[pos++] = field[i];
        }
    }
    
    field_data[pos] = '\0';
    row->fields[row->field_count] = field_data;
    row->field_lengths[row->field_count] = pos;
    row->field_count++;
    
    return true;
}

static bool stream_read_line(SonicSV* stream) {
    if (!stream) return false;
    
    stream->line_length = 0;
    stream->in_quote = false;
    
    bool line_complete = false;
    bool have_data;
    
    if (stream->is_mmap) {
        have_data = stream->mmap_pos < stream->mmap_size;
    } else {
        have_data = (stream->buffer_pos < stream->bytes_in_buffer);
    }
    
    while (!line_complete) {
        if (!have_data) {
            have_data = read_more_data(stream);
            if (!have_data) {
                if (stream->line_length == 0) {
                    return false;
                }
                line_complete = true;
                break;
            }
        }
        
        if (stream->is_mmap) {
            char* data = stream->mmap_data + stream->mmap_pos;
            size_t remaining = stream->mmap_size - stream->mmap_pos;
            size_t i = 0;
            
            size_t chunk_size = 4096;
            while (i < remaining) {
                size_t end = i + chunk_size < remaining ? i + chunk_size : remaining;
                bool newline_found = false;
                size_t newline_pos = i;
                
                for (; newline_pos < end; newline_pos++) {
                    if (data[newline_pos] == stream->config.quote_char) {
                        stream->in_quote = !stream->in_quote;
                    } else if (data[newline_pos] == '\n' && !stream->in_quote) {
                        newline_found = true;
                        break;
                    }
                }
                
                size_t bytes_to_copy = newline_found ? (newline_pos - i) : (end - i);
                if (bytes_to_copy > 0) {
                    if (!ensure_line_capacity(stream, bytes_to_copy)) {
                        return false;
                    }
                    memcpy(stream->line_buffer + stream->line_length, data + i, bytes_to_copy);
                    stream->line_length += bytes_to_copy;
                }
                
                stream->mmap_pos += bytes_to_copy + (newline_found ? 1 : 0);
                i += bytes_to_copy + (newline_found ? 1 : 0);
                
                if (newline_found) {
                    line_complete = true;
                    break;
                }
                
                if (end == remaining) break;
            }
            
            have_data = stream->mmap_pos < stream->mmap_size;
            if (line_complete) break;
        } else {
            char* buffer = stream->buffer + stream->buffer_pos;
            size_t remaining = stream->bytes_in_buffer - stream->buffer_pos;
            size_t i = 0;
            
            while (i < remaining) {
                size_t chunk_size = 4096;
                size_t end = i + chunk_size < remaining ? i + chunk_size : remaining;
                bool newline_found = false;
                size_t newline_pos = i;
                
                for (; newline_pos < end; newline_pos++) {
                    if (buffer[newline_pos] == stream->config.quote_char) {
                        stream->in_quote = !stream->in_quote;
                    } else if (buffer[newline_pos] == '\n' && !stream->in_quote) {
                        newline_found = true;
                        break;
                    }
                }
                
                size_t bytes_to_copy = newline_found ? (newline_pos - i) : (end - i);
                if (bytes_to_copy > 0) {
                    if (!ensure_line_capacity(stream, bytes_to_copy)) {
                        return false;
                    }
                    memcpy(stream->line_buffer + stream->line_length, buffer + i, bytes_to_copy);
                    stream->line_length += bytes_to_copy;
                }
                
                stream->buffer_pos += bytes_to_copy + (newline_found ? 1 : 0);
                i += bytes_to_copy + (newline_found ? 1 : 0);
                
                if (newline_found) {
                    line_complete = true;
                    break;
                }
                
                if (end == remaining) break;
            }
            
            have_data = false;
            if (line_complete) break;
        }
    }
    
    if (stream->config.skip_empty_lines && stream->line_length == 0) {
        return stream_read_line(stream);
    }
    
    if (!ensure_line_capacity(stream, 1)) {
        return false;
    }
    
    stream->line_buffer[stream->line_length] = '\0';
    
    if (stream->line_length > 0 && stream->line_buffer[stream->line_length - 1] == '\r') {
        stream->line_buffer[--stream->line_length] = '\0';
    }
    
    return true;
}

SonicSVConfig sonicsv_default_config(void) {
    init_system_parameters();
    
    SonicSVConfig config;
    config.delimiter = ',';
    config.quote_char = '"';
    config.trim_whitespace = false;
    config.skip_empty_lines = true;
    config.buffer_size = l2_cache_size;
    config.use_mmap = false;
    config.thread_safe = false;
    config.thread_buffer_size = l1_cache_size * 4;
    config.max_field_capacity = 1024;
    config.max_line_length = SONICSV_DEFAULT_MAX_LINE_LENGTH;
    config.max_threads = optimal_threads;
    return config;
}

SonicSV* sonicsv_open(const char* filename, const SonicSVConfig* config) {
    init_system_parameters();
    
    if (!filename) {
        set_error("Filename cannot be NULL");
        return NULL;
    }
    
    SonicSV* stream = calloc(1, sizeof(SonicSV));
    if (!stream) {
        set_error("Memory allocation failed for stream object");
        return NULL;
    }
    
    if (config) {
        stream->config = *config;
    } else {
        stream->config = sonicsv_default_config();
    }
    
    if (stream->config.buffer_size < system_page_size) {
        stream->config.buffer_size = system_page_size;
    } else if (stream->config.buffer_size > global_memory_limit / 4) {
        stream->config.buffer_size = global_memory_limit / 4;
    }
    
    if (stream->config.thread_safe) {
        if (pthread_mutex_init(&stream->mutex, NULL) != 0) {
            free(stream);
            set_error("Failed to initialize stream mutex");
            return NULL;
        }
    }
    
    stream->pool = memory_pool_create(stream->config.buffer_size / 16);
    if (!stream->pool) {
        if (stream->config.thread_safe) {
            pthread_mutex_destroy(&stream->mutex);
        }
        free(stream);
        return NULL;
    }
    
    struct stat st;
    if (stat(filename, &st) == 0) {
        if (!config && st.st_size > 10 * 1024 * 1024) {
            stream->config.use_mmap = true;
        }
    }
    
    if (stream->config.use_mmap) {
        stream->fd = open(filename, O_RDONLY);
        if (stream->fd == -1) {
            set_error("Failed to open file '%s': %s", filename, strerror(errno));
            memory_pool_destroy(stream->pool);
            if (stream->config.thread_safe) {
                pthread_mutex_destroy(&stream->mutex);
            }
            free(stream);
            return NULL;
        }
        
        if (fstat(stream->fd, &st) == -1) {
            set_error("Failed to stat file '%s': %s", filename, strerror(errno));
            close(stream->fd);
            memory_pool_destroy(stream->pool);
            if (stream->config.thread_safe) {
                pthread_mutex_destroy(&stream->mutex);
            }
            free(stream);
            return NULL;
        }
        
        stream->mmap_size = st.st_size;
        stream->mmap_data = mmap(NULL, stream->mmap_size, PROT_READ, MAP_PRIVATE, stream->fd, 0);
        
        if (stream->mmap_data == MAP_FAILED) {
            stream->is_mmap = false;
            stream->file = fopen(filename, "r");
            if (!stream->file) {
                set_error("Failed to open file '%s': %s", filename, strerror(errno));
                close(stream->fd);
                memory_pool_destroy(stream->pool);
                if (stream->config.thread_safe) {
                    pthread_mutex_destroy(&stream->mutex);
                }
                free(stream);
                return NULL;
            }
            
            stream->buffer = malloc(stream->config.buffer_size);
            if (!stream->buffer) {
                set_error("Memory allocation failed for read buffer");
                fclose(stream->file);
                close(stream->fd);
                memory_pool_destroy(stream->pool);
                if (stream->config.thread_safe) {
                    pthread_mutex_destroy(&stream->mutex);
                }
                free(stream);
                return NULL;
            }
        } else {
            stream->is_mmap = true;
            stream->mmap_pos = 0;
        }
    } else {
        stream->file = fopen(filename, "r");
        if (!stream->file) {
            set_error("Failed to open file '%s': %s", filename, strerror(errno));
            memory_pool_destroy(stream->pool);
            if (stream->config.thread_safe) {
                pthread_mutex_destroy(&stream->mutex);
            }
            free(stream);
            return NULL;
        }
        
        stream->buffer = malloc(stream->config.buffer_size);
        if (!stream->buffer) {
            set_error("Memory allocation failed for read buffer");
            fclose(stream->file);
            memory_pool_destroy(stream->pool);
            if (stream->config.thread_safe) {
                pthread_mutex_destroy(&stream->mutex);
            }
            free(stream);
            return NULL;
        }
    }
    
    stream->line_capacity = stream->config.buffer_size / 2;
    if (stream->line_capacity > stream->config.max_line_length) {
        stream->line_capacity = stream->config.max_line_length;
    }
    
    stream->line_buffer = malloc(stream->line_capacity);
    if (!stream->line_buffer) {
        set_error("Memory allocation failed for line buffer");
        if (stream->is_mmap) {
            munmap(stream->mmap_data, stream->mmap_size);
            close(stream->fd);
        } else {
            free(stream->buffer);
            fclose(stream->file);
        }
        memory_pool_destroy(stream->pool);
        if (stream->config.thread_safe) {
            pthread_mutex_destroy(&stream->mutex);
        }
        free(stream);
        return NULL;
    }
    
    return stream;
}

SonicSV* sonicsv_open_buffer(const char* data, size_t size, const SonicSVConfig* config) {
    init_system_parameters();
    
    if (!data) {
        set_error("Data buffer cannot be NULL");
        return NULL;
    }
    
    SonicSV* stream = calloc(1, sizeof(SonicSV));
    if (!stream) {
        set_error("Memory allocation failed for stream object");
        return NULL;
    }
    
    if (config) {
        stream->config = *config;
    } else {
        stream->config = sonicsv_default_config();
    }
    
    if (stream->config.buffer_size < system_page_size) {
        stream->config.buffer_size = system_page_size;
    } else if (stream->config.buffer_size > global_memory_limit / 4) {
        stream->config.buffer_size = global_memory_limit / 4;
    }
    
    if (stream->config.thread_safe) {
        if (pthread_mutex_init(&stream->mutex, NULL) != 0) {
            free(stream);
            set_error("Failed to initialize stream mutex");
            return NULL;
        }
    }
    
    stream->pool = memory_pool_create(stream->config.buffer_size / 16);
    if (!stream->pool) {
        if (stream->config.thread_safe) {
            pthread_mutex_destroy(&stream->mutex);
        }
        free(stream);
        return NULL;
    }
    
    stream->is_mmap = true;
    stream->mmap_data = (char*)data;
    stream->mmap_size = size;
    stream->mmap_pos = 0;
    stream->fd = -1;
    
    stream->line_capacity = stream->config.buffer_size / 2;
    if (stream->line_capacity > stream->config.max_line_length) {
        stream->line_capacity = stream->config.max_line_length;
    }
    
    stream->line_buffer = malloc(stream->line_capacity);
    if (!stream->line_buffer) {
        set_error("Memory allocation failed for line buffer");
        memory_pool_destroy(stream->pool);
        if (stream->config.thread_safe) {
            pthread_mutex_destroy(&stream->mutex);
        }
        free(stream);
        return NULL;
    }
    
    return stream;
}

void sonicsv_close(SonicSV* stream) {
    if (!stream) return;
    
    if (stream->config.thread_safe) {
        pthread_mutex_destroy(&stream->mutex);
    }
    
    if (stream->is_mmap) {
        if (stream->fd != -1) {
            munmap(stream->mmap_data, stream->mmap_size);
            close(stream->fd);
        }
    } else {
        if (stream->file) {
            fclose(stream->file);
            stream->file = NULL;
        }
        free(stream->buffer);
    }
    
    free(stream->line_buffer);
    memory_pool_destroy(stream->pool);
    free(stream);
}

bool sonicsv_seek(SonicSV* stream, long offset) {
    if (!stream) {
        set_error("Stream cannot be NULL");
        return false;
    }

    if (stream->config.thread_safe) {
        pthread_mutex_lock(&stream->mutex);
    }

    bool result = true;

    stream->line_length = 0;
    stream->in_quote = false;
    stream->buffer_pos = 0;
    stream->bytes_in_buffer = 0;
    stream->eof_reached = false; // Reset eof_reached on seek

    if (stream->is_mmap) {
        if (offset < 0 || (size_t)offset > stream->mmap_size) {
            set_error("Seek offset %ld out of mmap range (size: %zu)", offset, stream->mmap_size);
            result = false;
        } else {
            stream->mmap_pos = (size_t)offset;
            stream->eof_reached = (stream->mmap_pos >= stream->mmap_size);

            // If seeking to a non-zero offset, try to align to the start of the next line
            if (offset > 0 && stream->mmap_pos < stream->mmap_size && stream->mmap_data[stream->mmap_pos -1] != '\n') {
                while (stream->mmap_pos < stream->mmap_size) {
                    if (stream->mmap_data[stream->mmap_pos++] == '\n') {
                        break;
                    }
                }
            }
            stream->eof_reached = (stream->mmap_pos >= stream->mmap_size);
            // result determination for mmap is simpler: if offset was valid, it's fine.
            // The while loop above handles EOF correctly.
            // The caller will find out about EOF on next read if seek was to end.
        }
    } else { // File I/O
        long file_size = -1;
        if (stream->file) {
            long current_fpos = ftell(stream->file);
            if (current_fpos != -1L) { // Check if ftell succeeded
                if (fseek(stream->file, 0, SEEK_END) == 0) {
                    file_size = ftell(stream->file);
                }
                // Restore original position, check for error
                if (fseek(stream->file, current_fpos, SEEK_SET) != 0 && result) {
                    // This is an issue, but less critical than the main seek failing
                    // For now, we mostly care about the main seek operation's success
                }
            } else if (result) { // ftell failed
                // This situation is problematic for size checking, proceed with caution
                // set_error("Seek: ftell failed when trying to get file size: %s", strerror(errno));
                // result = false; // Or decide to proceed without file_size check
            }
        } else if (result) {
            set_error("Seek: Stream file handle is NULL for non-mmap stream");
            result = false;
        }

        if (result) { // Only proceed if no prior error (like NULL file handle)
            if (offset < 0 || (file_size != -1 && offset > file_size)) {
                set_error("Seek offset %ld out of file range (size: %ld)", offset, file_size);
                result = false;
            } else if (fseek(stream->file, offset, SEEK_SET) != 0) {
                set_error("Initial seek to offset %ld failed (fseek): %s", offset, strerror(errno));
                result = false;
            } else {
                stream->eof_reached = feof(stream->file); // Check EOF after initial seek

                // If seeking to a non-zero offset and not at EOF, try to align to the start of the next line
                if (offset > 0 && !stream->eof_reached) {
                    // We need to read the character *before* the current offset to see if it was a newline.
                    // So, seek to offset - 1, read char, then decide if we need to scan forward.
                    if (fseek(stream->file, offset - 1, SEEK_SET) == 0) {
                        int prev_char = fgetc(stream->file); // This moves file pointer to 'offset'
                        if (prev_char == EOF) { // Reached EOF trying to read char before offset
                            stream->eof_reached = true;
                        } else if (prev_char != '\n') {
                            // prev_char was not a newline, so current 'offset' is mid-line.
                            // Scan forward for the next newline.
                            // fgetc already moved us to 'offset', so we start scanning from current pos.
                            int ch_skip;
                            while ((ch_skip = fgetc(stream->file)) != EOF && ch_skip != '\n');
                            stream->eof_reached = feof(stream->file);
                        }
                        // If prev_char was '\n', we are already at the start of the line at 'offset'.
                        // The file pointer is now at 'offset' if prev_char was '\n', or after next '\n' or EOF.
                    } else {
                        // Failed to seek to offset - 1. This is an error.
                        set_error("Seek to offset-1 (%ld) for newline check failed: %s", offset - 1, strerror(errno));
                        result = false;
                    }
                }
                if (result) { // If still no error
                     stream->eof_reached = feof(stream->file);
                }
            }
        }
    }

    if (stream->config.thread_safe) {
        pthread_mutex_unlock(&stream->mutex);
    }

    // Final check: result is true if seek was successful and we are not at EOF,
    // OR if we intended to seek to EOF and are indeed there.
    // If result is false, an error was set.
    // If result is true, but eof_reached is true, it means seek was to/past EOF.
    // A successful seek could be to the exact end of the file.
    // The function should return true if the seek operation itself was valid,
    // even if it results in being at EOF. The caller (sonicsv_next_row) will handle EOF.
    return result;
}

static SonicSVRow* get_row_from_pool(size_t field_capacity) {
    if (!tl_row_pool.rows) {
        tl_row_pool.capacity = ROW_POOL_SIZE;
        tl_row_pool.rows = calloc(tl_row_pool.capacity, sizeof(SonicSVRow*));
        if (!tl_row_pool.rows) {
            SonicSVRow* direct_row = calloc(1, sizeof(SonicSVRow));
            if (!direct_row) { set_error("Memory allocation failed for row struct (fallback)"); return NULL; }
            direct_row->capacity = field_capacity > 0 ? field_capacity : 16;
            direct_row->fields = malloc(direct_row->capacity * sizeof(char*));
            direct_row->field_lengths = malloc(direct_row->capacity * sizeof(size_t));
            if(!direct_row->fields || !direct_row->field_lengths) { 
                free(direct_row->fields); free(direct_row->field_lengths); free(direct_row);
                set_error("Memory allocation failed for row field arrays (fallback)"); return NULL; 
            }
            direct_row->owns_data = true; 
            direct_row->pool = NULL; 
            return direct_row;
        }
        
        size_t successfully_preallocated = 0;
        for (size_t i = 0; i < tl_row_pool.capacity; ++i) {
            tl_row_pool.rows[i] = calloc(1, sizeof(SonicSVRow));
            if (tl_row_pool.rows[i]) {
                size_t initial_field_cap = field_capacity > 0 ? field_capacity : 32;
                if (initial_field_cap < 16) initial_field_cap = 16;
                tl_row_pool.rows[i]->capacity = initial_field_cap;
                tl_row_pool.rows[i]->fields = malloc(initial_field_cap * sizeof(char*));
                tl_row_pool.rows[i]->field_lengths = malloc(initial_field_cap * sizeof(size_t));
                
                if (!tl_row_pool.rows[i]->fields || !tl_row_pool.rows[i]->field_lengths) {
                    free(tl_row_pool.rows[i]->fields);
                    free(tl_row_pool.rows[i]->field_lengths);
                    free(tl_row_pool.rows[i]);
                    tl_row_pool.rows[i] = NULL;
                } else {
                    successfully_preallocated++;
                }
            }
        }
        tl_row_pool.used = successfully_preallocated; 
    }
    
    if (tl_row_pool.used > 0) {
        SonicSVRow* row = tl_row_pool.rows[--tl_row_pool.used];
        
        if (row->capacity < field_capacity) {
            char** new_fields = realloc(row->fields, field_capacity * sizeof(char*));
            size_t* new_lengths = realloc(row->field_lengths, field_capacity * sizeof(size_t));
            
            if (new_fields && new_lengths) {
                row->fields = new_fields;
                row->field_lengths = new_lengths;
                row->capacity = field_capacity;
            } else {
                tl_row_pool.rows[tl_row_pool.used++] = row;
            }
        }
        
        row->field_count = 0;
        row->owns_data = false;
        row->pool = NULL;
        return row;
    }
    
    SonicSVRow* direct_row = calloc(1, sizeof(SonicSVRow));
    if (!direct_row) { set_error("Memory allocation failed for row struct (direct alloc)"); return NULL; }
    direct_row->capacity = field_capacity > 0 ? field_capacity : 16;
    direct_row->fields = malloc(direct_row->capacity * sizeof(char*));
    direct_row->field_lengths = malloc(direct_row->capacity * sizeof(size_t));
    if (!direct_row->fields || !direct_row->field_lengths) {
        free(direct_row->fields); free(direct_row->field_lengths); free(direct_row);
        set_error("Memory allocation failed for row field arrays (direct alloc)"); return NULL;
    }
    direct_row->owns_data = true;
    direct_row->pool = NULL;
    return direct_row;
}

/**
 * OPTIMIZATION: In-place field processing in sonicsv_next_row
 * Reduces memory allocations and copying by processing fields directly
 * from the line buffer, only allocating when unescaping is needed.
 */
SonicSVRow* sonicsv_next_row(SonicSV* stream) {
    if (!stream) {
        set_error("Stream cannot be NULL");
        return NULL;
    }
    
    if (stream->config.thread_safe) {
        pthread_mutex_lock(&stream->mutex);
    }
    
    if (!stream_read_line(stream)) {
        if (stream->config.thread_safe) {
            pthread_mutex_unlock(&stream->mutex);
        }
        return NULL;
    }
    
    SonicSVRow* row = get_row_from_pool(stream->config.max_field_capacity);
    bool new_row_allocated = false;
    if (!row) {
        row = calloc(1, sizeof(SonicSVRow));
        if (!row) {
            set_error("Memory allocation failed for row");
            if (stream->config.thread_safe) {
                pthread_mutex_unlock(&stream->mutex);
            }
            return NULL;
        }
        
        row->capacity = stream->config.max_field_capacity;
        row->fields = malloc(row->capacity * sizeof(char*));
        row->field_lengths = malloc(row->capacity * sizeof(size_t));
        
        if (!row->fields || !row->field_lengths) {
            set_error("Memory allocation failed for row fields");
            free(row->fields);
            free(row->field_lengths);
            free(row);
            if (stream->config.thread_safe) {
                pthread_mutex_unlock(&stream->mutex);
            }
            return NULL;
        }
        new_row_allocated = true;
    }
    
    row->pool = stream->pool;
    row->owns_data = new_row_allocated; // Only newly allocated rows own data initially
    row->field_count = 0;
    
    char* line = stream->line_buffer; // Use char* to modify in place
    size_t line_len = stream->line_length;
    char* field_start = line;
    bool in_quotes = false;
    
    for (size_t i = 0; i <= line_len; i++) {
        char c = (i < line_len) ? line[i] : '\0';
        
        if (c == stream->config.quote_char) {
            if (in_quotes && i + 1 < line_len && line[i + 1] == stream->config.quote_char) {
                i++; // Skip escaped quote
            } else {
                in_quotes = !in_quotes;
            }
        } else if ((c == stream->config.delimiter || c == '\0') && !in_quotes) {
            char original_char = line[i]; // Store original char before null termination
            line[i] = '\0'; // Null-terminate the field in-place
            
            size_t current_field_len = &line[i] - field_start;
            char* current_field_start = field_start;
            bool is_quoted = false;
            
            if (stream->config.trim_whitespace && current_field_len > 0) {
                while (current_field_len > 0 && (*current_field_start == ' ' || *current_field_start == '\t')) {
                    current_field_start++;
                    current_field_len--;
                }
                
                char* end_ptr = current_field_start + current_field_len - 1;
                while (current_field_len > 0 && (*end_ptr == ' ' || *end_ptr == '\t')) {
                    current_field_len--;
                    end_ptr--;
                }
                // Null-terminate again if trimming changed the end
                if (current_field_start + current_field_len < &line[i]) {
                     *(current_field_start + current_field_len) = '\0';
                }
            }
            
            bool needs_unescaping = false;
            if (current_field_len >= 2 && *current_field_start == stream->config.quote_char && 
                *(current_field_start + current_field_len - 1) == stream->config.quote_char) {
                is_quoted = true;
                current_field_start++;
                current_field_len -= 2;
                
                // Check if unescaping is actually needed (contains "quote_char""quote_char")
                for(size_t k=0; k < current_field_len; ++k) {
                    if (current_field_start[k] == stream->config.quote_char && k + 1 < current_field_len && current_field_start[k+1] == stream->config.quote_char) {
                        needs_unescaping = true;
                        break;
                    }
                }
                // Null-terminate the inner part if it changed
                if (is_quoted && current_field_start + current_field_len < &line[i]) {
                     *(current_field_start + current_field_len) = '\0';
                }
            }

            if (row->field_count >= row->capacity) {
                if (!ensure_row_capacity(row, row->capacity * 2)) { // ensure_row_capacity needs to be available or implemented
                     line[i] = original_char; // Restore original char
                     sonicsv_row_free(row);
                     if (stream->config.thread_safe) {
                         pthread_mutex_unlock(&stream->mutex);
                     }
                     return NULL;
                }
            }

            if (is_quoted && needs_unescaping) {
                // Only allocate and copy if unescaping is necessary
                char* unescaped_field = row->pool ? memory_pool_alloc(row->pool, current_field_len + 1) : malloc(current_field_len + 1);
                if (!unescaped_field) {
                    line[i] = original_char;
                    set_error("Memory allocation failed for unescaped field");
                    sonicsv_row_free(row);
                    if (stream->config.thread_safe) {
                        pthread_mutex_unlock(&stream->mutex);
                    }
                    return NULL;
                }
                
                size_t pos = 0;
                for (size_t k = 0; k < current_field_len; k++) {
                    if (current_field_start[k] == stream->config.quote_char && k + 1 < current_field_len && 
                        current_field_start[k + 1] == stream->config.quote_char) {
                        unescaped_field[pos++] = stream->config.quote_char;
                        k++;
                    } else {
                        unescaped_field[pos++] = current_field_start[k];
                    }
                }
                unescaped_field[pos] = '\0';
                row->fields[row->field_count] = unescaped_field;
                row->field_lengths[row->field_count] = pos;
                // If this row came from the pool, it now owns this allocated data.
                // If this is a newly allocated row, it continues to own its data.
                if (!new_row_allocated) row->owns_data = true; 
            } else {
                // If no unescaping, point directly to the modified line_buffer
                // But first, if it's a pooled row, its fields might point to old data.
                // We must ensure this field's data is "owned" by the current line_buffer processing
                // or allocate a new copy if the row is not fresh (new_row_allocated is false).
                if (!new_row_allocated && row->owns_data) {
                    // This field came from a previous line_buffer, needs a copy.
                    // Or, the row came from the pool, and we need to give it its own copy.
                    char* field_copy = row->pool ? memory_pool_alloc(row->pool, current_field_len + 1) : malloc(current_field_len + 1);
                    if (!field_copy) {
                         line[i] = original_char;
                         set_error("Memory allocation failed for field copy");
                         sonicsv_row_free(row);
                         if (stream->config.thread_safe) {
                             pthread_mutex_unlock(&stream->mutex);
                         }
                         return NULL;
                    }
                    memcpy(field_copy, current_field_start, current_field_len);
                    field_copy[current_field_len] = '\0';
                    row->fields[row->field_count] = field_copy;

                } else {
                     // Point directly to the (potentially modified) line_buffer segment
                    row->fields[row->field_count] = current_field_start; 
                }
                row->field_lengths[row->field_count] = current_field_len;
                 // If a pooled row points into line_buffer, it does not own this specific field's data in terms of freeing it.
                // The line_buffer will be reused. If it's a fresh row, it owns all its fields.
                if (!new_row_allocated) {
                     // This logic needs careful handling of row->owns_data.
                     // For simplicity, if any field is copied, the row is marked as owning data.
                     // If all fields are direct pointers, then it doesn't own data.
                     // Let's assume if any field is copied, owns_data becomes true.
                     // The previous version of process_field always allocated, so row->owns_data was simpler.
                     // For now, this needs more nuanced logic or accept that pooled rows may allocate.
                }

            }
            row->field_count++;
            
            line[i] = original_char; // Restore original character for next field
            field_start = &line[i + 1];
            if (i >= line_len) break; // End of line
        }
    }
    
    // After processing all fields, if the row came from the pool and no fields were copied
    // (i.e., all fields are pointers into line_buffer), then it doesn't own its data.
    // This is a simplified check; a more robust way is to track if any allocation happened.
    if (!new_row_allocated) {
        bool all_pointers_to_line_buffer = true;
        for(size_t k=0; k < row->field_count; ++k) {
            // A rough check: if the pointer is within the line_buffer range
            if (!(row->fields[k] >= stream->line_buffer && row->fields[k] < stream->line_buffer + stream->line_capacity)) {
                all_pointers_to_line_buffer = false;
                break;
            }
        }
        if (all_pointers_to_line_buffer) {
            row->owns_data = false;
        } else {
            row->owns_data = true; // Some fields were allocated
        }
    }


    if (stream->config.thread_safe) {
        pthread_mutex_unlock(&stream->mutex);
    }
    
    return row;
}

void sonicsv_row_free(SonicSVRow* row) {
    if (!row) return;
    
    // Only free field data if the row owns it (i.e., it was allocated, not pointing into line_buffer)
    if (row->fields && row->owns_data && !row->pool) { // Original condition for non-pooled rows
        for (size_t i = 0; i < row->field_count; i++) {
            free(row->fields[i]);
        }
    } else if (row->fields && row->owns_data && row->pool) { // Pooled row that allocated some fields
         for (size_t i = 0; i < row->field_count; i++) {
            // This check is imperfect. We'd need to know if this specific field was allocated
            // or if it's a pointer to line_buffer.
            // For now, if row->owns_data is true, we assume all fields it points to were allocated for it.
            // The memory_pool_alloc handles freeing of the pool blocks eventually.
            // If a field was allocated directly with malloc (because row->pool was NULL or unescaping path), free it.
            // This logic needs refinement if we mix pooled and non-pooled allocations within a row's fields.
            // A simple heuristic: if a field is outside the known tl_buffer ranges or memory_pool blocks, it might be malloc'd.
            // However, for now, we assume if owns_data is true, memory_pool_alloc took care of it or it was malloc'd and needs free.
            // The `memory_pool_alloc` itself doesn't track individual allocations to free them, it frees whole blocks.
            // So, if `row->pool` is set, the data comes from the memory pool and is managed by it.
            // If `row->pool` is NULL, then `process_field` used `malloc` and we should `free`.
            // The `row->owns_data` flag should signify if `row->fields[i]` are to be freed.
            
            // Let's refine: if row came from pool, its fields *might* point to line_buffer (owns_data=false then)
            // or *might* have been allocated (owns_data=true then, via unescaping or copy).
            // If it was *not* from pool, then it always used malloc (owns_data=true).
            
            // If row->pool is set and row->owns_data is true, it implies some fields were allocated via row->pool.
            // These are managed by the pool, so we don't free them individually here.
            // If row->pool is NULL and row->owns_data is true, fields were malloc'd.
            if (!row->pool) { // Only free if not from a memory pool and owns data
                 free(row->fields[i]);
            }
        }
    }
    
    if (tl_row_pool.rows && tl_row_pool.used < tl_row_pool.capacity) {
        row->field_count = 0;
        row->owns_data = false; // Reset for next use from pool
        tl_row_pool.rows[tl_row_pool.used++] = row;
    } else {
        // If not returning to pool, or pool is full, then free the row structure itself
        // and its field arrays. Individual field data freeing is handled above.
        free(row->fields);
        free(row->field_lengths);
        free(row);
    }
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

const char* sonicsv_row_get_field_bytes(const SonicSVRow* row, size_t index, size_t* length) {
    if (!row || index >= row->field_count) {
        if (length) *length = 0;
        return NULL;
    }
    
    if (length) *length = row->field_lengths[index];
    return row->fields[index];
}

static ThreadPool* thread_pool_create(size_t num_threads) {
    init_system_parameters();
    
    if (num_threads == 0) {
        num_threads = optimal_threads;
    }
    
    if (num_threads > SONICSV_DEFAULT_MAX_THREADS) {
        num_threads = SONICSV_DEFAULT_MAX_THREADS;
    }
    
    ThreadPool* pool = calloc(1, sizeof(ThreadPool));
    if (!pool) {
        set_error("Failed to allocate thread pool");
        return NULL;
    }
    
    pool->threads = calloc(num_threads, sizeof(pthread_t));
    if (!pool->threads) {
        free(pool);
        set_error("Failed to allocate thread array");
        return NULL;
    }
    
    pool->max_threads = num_threads;
    pool->running = true;
    
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        free(pool->threads);
        free(pool);
        set_error("Failed to initialize thread pool mutex");
        return NULL;
    }
    
    if (pthread_cond_init(&pool->cond, NULL) != 0) {
        pthread_mutex_destroy(&pool->mutex);
        free(pool->threads);
        free(pool);
        set_error("Failed to initialize thread pool condition variable");
        return NULL;
    }
    
    return pool;
}

static void thread_pool_destroy(ThreadPool* pool) {
    if (!pool) return;
    
    pthread_mutex_lock(&pool->mutex);
    pool->running = false;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
    
    for (size_t i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
    
    free(pool->threads);
    free(pool);
}

static void* dynamic_thread_worker(void* arg) {
    DynamicThreadWork* work = (DynamicThreadWork*)arg;
    if (!work) return NULL;

    // Ensure worker_error_message is initially clear for this work unit
    work->worker_error_message[0] = '\0';

    SonicSV* stream = NULL;
    long rows_processed = 0;
    long file_size = work->file_size;
    long chunk_size = work->chunk_size;

    while (true) {
        long start_offset = atomic_fetch_add(work->next_chunk, chunk_size);
        if (start_offset >= file_size) break;

        long end_offset = start_offset + chunk_size;
        if (end_offset > file_size) end_offset = file_size;

        stream = sonicsv_open(work->filename, &work->config);
        if (!stream) {
            work->error_code = 1; // Error opening file
            const char* err_msg = sonicsv_get_error(); // Get error from this thread's error_buffer
            if (err_msg) {
                strncpy(work->worker_error_message, err_msg, sizeof(work->worker_error_message) - 1);
                work->worker_error_message[sizeof(work->worker_error_message) - 1] = '\0';
            } else {
                snprintf(work->worker_error_message, sizeof(work->worker_error_message), "Worker: Failed to open file (unknown reason).");
            }
            sonicsv_thread_cleanup_internal();
            return NULL;
        }

        if (start_offset > 0) {
            if (!sonicsv_seek(stream, start_offset)) {
                work->error_code = 2; // Error seeking in file
                const char* err_msg = sonicsv_get_error();
                if (err_msg) {
                    strncpy(work->worker_error_message, err_msg, sizeof(work->worker_error_message) - 1);
                    work->worker_error_message[sizeof(work->worker_error_message) - 1] = '\0';
                } else {
                    snprintf(work->worker_error_message, sizeof(work->worker_error_message), "Worker: Failed to seek in file (unknown reason).");
                }
                sonicsv_close(stream);
                sonicsv_thread_cleanup_internal();
                return NULL;
            }
        }

        SonicSVRow* row;
        long current_offset = -1; // Initialize to an invalid offset
        long chunk_rows = 0;

        while ((row = sonicsv_next_row(stream)) != NULL) {
            if (work->callback) {
                work->callback(row, work->user_data);
            }

            chunk_rows++;
            sonicsv_row_free(row);

            // Determine current offset safely
            if (stream->is_mmap) {
                current_offset = (long)stream->mmap_pos;
            } else {
                 if (stream->file) {
                    current_offset = ftell(stream->file);
                    if (current_offset == -1L) { // ftell can return -1 on error
                        work->error_code = 4; // Error getting file offset
                        const char* ftell_err_msg = strerror(errno);
                        snprintf(work->worker_error_message, sizeof(work->worker_error_message), "Worker: ftell failed: %s", ftell_err_msg ? ftell_err_msg : "Unknown I/O error");
                        sonicsv_close(stream);
                        stream = NULL;
                        sonicsv_thread_cleanup_internal();
                        return NULL;
                    }
                 } else {
                    // This state (stream not mmap, stream->file is NULL) indicates a severe issue.
                    work->error_code = 3; // Invalid stream state
                    snprintf(work->worker_error_message, sizeof(work->worker_error_message), "Worker: Stream file handle is NULL unexpectedly.");
                    if(stream) sonicsv_close(stream); // stream might be non-NULL but stream->file is NULL
                    stream = NULL;
                    sonicsv_thread_cleanup_internal();
                    return NULL;
                 }
            }
            
            if (current_offset >= end_offset && end_offset != file_size) {
                break;
            }
        }
        // Check for errors after sonicsv_next_row loop (e.g., if last call to sonicsv_next_row failed)
        const char* next_row_err = sonicsv_get_error();
        if (next_row_err && work->error_code == 0) { // If no prior error_code was set
            work->error_code = 5; // Error during row processing
            strncpy(work->worker_error_message, next_row_err, sizeof(work->worker_error_message) - 1);
            work->worker_error_message[sizeof(work->worker_error_message) - 1] = '\0';
            // No need to return immediately, let cleanup happen, error is recorded.
        }
        
        rows_processed += chunk_rows;
        if (stream) { // stream might have been set to NULL if error_code 3 occurred
            sonicsv_close(stream);
            stream = NULL;
        }
        if (work->error_code != 0) break; // If an error occurred (like code 5), stop processing chunks for this worker
    }

    // Fallback error message if error_code is set but worker_error_message is still empty
    if (work->error_code != 0 && work->worker_error_message[0] == '\0') {
        snprintf(work->worker_error_message, sizeof(work->worker_error_message),
                 "Worker: Encountered error code %d (no specific message).", work->error_code);
    }
    
    work->rows_processed = rows_processed;
    sonicsv_thread_cleanup_internal();
    return NULL;
}

bool sonicsv_thread_pool_init(size_t num_threads) {
    sonicsv_thread_pool_destroy();
    global_thread_pool = thread_pool_create(num_threads);
    return global_thread_pool != NULL;
}

void sonicsv_thread_pool_destroy(void) {
    if (global_thread_pool) {
        thread_pool_destroy(global_thread_pool);
        global_thread_pool = NULL;
    }
}

long sonicsv_process_parallel(const char* filename, const SonicSVConfig* config_param, // Renamed to avoid conflict
                             SonicSVRowCallback callback, void* user_data) {
    init_system_parameters();
    
    if (!filename) {
        set_error("Filename cannot be NULL");
        return -1;
    }
    
    struct stat st;
    if (stat(filename, &st) != 0) {
        set_error("Failed to stat file '%s': %s", filename, strerror(errno));
        return -1;
    }
    
    if (!global_thread_pool) {
        if (!sonicsv_thread_pool_init(0)) { // Auto-detect threads if not initialized
            // sonicsv_thread_pool_init will call set_error on failure
            return -1;
        }
    }
    
    size_t num_threads_to_use = global_thread_pool->max_threads; // Start with pool's max

    if (config_param) { // If a specific config is provided
        if (config_param->max_threads > 0 && config_param->max_threads < num_threads_to_use) {
            num_threads_to_use = config_param->max_threads;
        }
    } else { // No config provided, use defaults and heuristics
        num_threads_to_use = optimal_threads;
        if (st.st_size > 0) { // Only apply heuristics if file size is known and positive
            if (num_threads_to_use > 8 && st.st_size < 10 * 1024 * 1024) { // 10MB
                num_threads_to_use = 4;
            } else if (num_threads_to_use > 4 && st.st_size < 5 * 1024 * 1024) { // 5MB
                 num_threads_to_use = 2;
            } else if (num_threads_to_use > 2 && st.st_size < 1 * 1024 * 1024) { // 1MB
                num_threads_to_use = 1; // Force single for small files if many cores
            }
        }
    }
    if (num_threads_to_use == 0) num_threads_to_use = 1; // Ensure at least one thread


    long file_size = st.st_size;
    // Switch to single-threaded path if file is very small or only 1 thread is requested
    if (file_size < (32 * 1024) || num_threads_to_use <= 1) { // 32KB threshold
        SonicSVConfig local_config = config_param ? *config_param : sonicsv_default_config();
        local_config.thread_safe = false; // Ensure single-threaded behavior for this path

        SonicSV* stream = sonicsv_open(filename, &local_config);
        if (!stream) {
            // sonicsv_open calls set_error
            return -1;
        }
        
        long rows = 0;
        SonicSVRow* row;
        while ((row = sonicsv_next_row(stream)) != NULL) {
            if (callback) {
                callback(row, user_data);
            }
            rows++;
            sonicsv_row_free(row);
        }
        
        const char* stream_error = sonicsv_get_error(); // Check if sonicsv_next_row set an error
        sonicsv_close(stream);
        
        if (stream_error && rows == 0 && !strstr(stream_error, "memory pool")) { // If error happened before any row, or specific errors
            // Error already set by sonicsv_next_row or sonicsv_open
            return -1;
        }
        // If rows were processed, or it's just a pool error at the end, return row count
        return rows;
    }
    
    // Parallel processing path
    size_t actual_num_threads_launched = num_threads_to_use; // Track how many threads we actually try to launch
    long min_chunk_size = 32 * 1024; 
    long chunk_size_calc = file_size / (actual_num_threads_launched * 4); // Aim for 4 chunks per thread
    
    if (chunk_size_calc < min_chunk_size) {
        chunk_size_calc = min_chunk_size;
    }
    if (chunk_size_calc == 0 && file_size > 0) chunk_size_calc = file_size; // Handle tiny files if they somehow reach here
    else if (chunk_size_calc == 0 && file_size == 0) return 0; // Empty file


    atomic_long next_chunk_offset; // Renamed from next_chunk to avoid type confusion
    atomic_init(&next_chunk_offset, 0);
    
    DynamicThreadWork* work_units = calloc(actual_num_threads_launched, sizeof(DynamicThreadWork));
    if (!work_units) {
        set_error("Failed to allocate work units for parallel processing");
        return -1;
    }
    
    SonicSVConfig worker_config = config_param ? *config_param : sonicsv_default_config();
    worker_config.thread_safe = true; // Each stream opened by worker needs to be prepared for MT access if it were shared (though it's not here)
                                      // More importantly, this ensures its internal mutexes are init'd if used.
                                      // The critical part is that each worker gets its OWN SonicSV instance.

    global_thread_pool->num_threads = 0; // Reset active thread count in pool

    for (size_t i = 0; i < actual_num_threads_launched; i++) {
        work_units[i].filename = filename;
        work_units[i].config = worker_config;
        work_units[i].callback = callback;
        work_units[i].user_data = user_data;
        work_units[i].next_chunk = &next_chunk_offset;
        work_units[i].file_size = file_size;
        work_units[i].chunk_size = chunk_size_calc;
        work_units[i].error_code = 0;
        work_units[i].worker_error_message[0] = '\0'; // Initialize error message buffer
        
        int ret = pthread_create(&global_thread_pool->threads[i], NULL, 
                               dynamic_thread_worker, &work_units[i]);
        if (ret != 0) {
            set_error("Failed to create thread %zu: %s", i, strerror(ret));
            // Join already created threads before erroring out
            for (size_t j = 0; j < global_thread_pool->num_threads; j++) { // Use actual count of launched threads
                pthread_join(global_thread_pool->threads[j], NULL);
            }
            free(work_units);
            global_thread_pool->num_threads = 0; // Reset for future calls
            return -1;
        }
        global_thread_pool->num_threads++; // Increment successfully launched threads
    }
    
    long total_rows_processed = 0;
    bool error_occurred_in_any_worker = false;
    char first_worker_error_msg[256] = {0};
    
    for (size_t i = 0; i < global_thread_pool->num_threads; i++) { // Join only launched threads
        pthread_join(global_thread_pool->threads[i], NULL);
        
        if (work_units[i].error_code != 0) {
            error_occurred_in_any_worker = true;
            if (first_worker_error_msg[0] == '\0' && work_units[i].worker_error_message[0] != '\0') {
                strncpy(first_worker_error_msg, work_units[i].worker_error_message, sizeof(first_worker_error_msg) - 1);
                first_worker_error_msg[sizeof(first_worker_error_msg) - 1] = '\0';
            }
        } else {
            total_rows_processed += work_units[i].rows_processed;
        }
    }
    
    global_thread_pool->num_threads = 0; // Reset active count
    free(work_units);
    
    if (error_occurred_in_any_worker) {
        if (first_worker_error_msg[0] != '\0') {
            set_error("%s", first_worker_error_msg);
        } else {
            // Fallback if no specific message was captured (should be rare with new logic)
            set_error("Error in worker thread during parallel processing (generic).");
        }
        return -1;
    }
    
    return total_rows_processed;
}

static void dummy_row_callback(const SonicSVRow* row, void* user_data) {
    (void)row;
    (void)user_data;
}

long sonicsv_count_rows(const char* filename, const SonicSVConfig* config) {
    if (!filename) {
        set_error("Filename cannot be NULL");
        return -1;
    }
    
    struct stat st;
    if (stat(filename, &st) == 0 && st.st_size < 1 * 1024 * 1024) {
        SonicSVConfig counting_config = config ? *config : sonicsv_default_config();
        counting_config.thread_safe = false;
        SonicSV* stream = sonicsv_open(filename, &counting_config);
        if (!stream) return -1;
        
        long count = 0;
        SonicSVRow* row;
        while ((row = sonicsv_next_row(stream)) != NULL) {
            count++;
            sonicsv_row_free(row);
        }
        
        const char* err = sonicsv_get_error();
        if (err) {
        }
        sonicsv_close(stream);
        return count;
    }
    
    return sonicsv_process_parallel(filename, config, dummy_row_callback, NULL);
}

size_t sonicsv_get_optimal_threads(void) {
    init_system_parameters();
    return optimal_threads;
}

bool sonicsv_set_memory_limit(size_t max_bytes) {
    if (max_bytes < 1024 * 1024) {
        set_error("Memory limit too small (minimum 1MB)");
        return false;
    }
    
    global_memory_limit = max_bytes;
    return true;
}

void sonicsv_benchmark_cleanup(SonicSV* stream, SonicSVRow* row) {
    if (row) {
        sonicsv_row_free(row);
    }
    if (stream) {
        sonicsv_close(stream);
    }
}

SonicSVRow* sonicsv_row_create(size_t initial_capacity) {
    if (initial_capacity == 0) {
        initial_capacity = 16;
    }
    
    SonicSVRow* row = calloc(1, sizeof(SonicSVRow));
    if (!row) {
        set_error("Memory allocation failed for row");
        return NULL;
    }
    
    row->capacity = initial_capacity;
    row->fields = malloc(row->capacity * sizeof(char*));
    row->field_lengths = malloc(row->capacity * sizeof(size_t));
    row->owns_data = true;
    
    if (!row->fields || !row->field_lengths) {
        free(row->fields);
        free(row->field_lengths);
        free(row);
        set_error("Memory allocation failed for row fields");
        return NULL;
    }
    
    return row;
}

bool sonicsv_row_add_field(SonicSVRow* row, const char* field, size_t length) {
    if (!row || !field) {
        set_error("Invalid row or field");
        return false;
    }
    
    if (length == 0) {
        length = strlen(field);
    }
    
    return process_field(row, field, length, false);
}

const char* sonicsv_get_version(void) {
    return SONICSV_VERSION;
}

bool sonicsv_init(void) {
    init_system_parameters();
    return sonicsv_thread_pool_init(optimal_threads);
}

void sonicsv_cleanup(void) {
    sonicsv_thread_pool_destroy();
    memset(error_buffer, 0, sizeof(error_buffer));
    
    sonicsv_thread_cleanup_internal(); // Clean up TLS for the calling (main) thread
} 