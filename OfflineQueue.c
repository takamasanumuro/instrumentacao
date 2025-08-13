#include "OfflineQueue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h> // For gzip compression

#define MAX_BATCH_SIZE 5000
#define MAX_LINE_LENGTH 2048
#define BATCH_BUFFER_SIZE (MAX_BATCH_SIZE * MAX_LINE_LENGTH)

static char g_log_file_path[256];
static char g_temp_log_file_path[256];

// --- Public Functions ---

void offline_queue_init(const char* log_file_path) {
    mkdir("logs", 0755); // Ensure the directory exists
    strncpy(g_log_file_path, log_file_path, sizeof(g_log_file_path) - 1);
    g_log_file_path[sizeof(g_log_file_path) - 1] = '\0';

    snprintf(g_temp_log_file_path, sizeof(g_temp_log_file_path), "%s.tmp", g_log_file_path);
}

void offline_queue_add(const char* line_protocol) {
    FILE* file = fopen(g_log_file_path, "a");
    if (file) {
        fprintf(file, "%s\n", line_protocol);
        fclose(file);
    } else {
        perror("Failed to open offline log file");
    }
}


// Helper function to process a batch of lines
static bool process_batch(send_batch_func_t send_func, void* user_context, char* line_batch[], int line_count) {
    if (line_count == 0) {
        return true;
    }

    // 1. Concatenate the batch into a single buffer
    size_t total_size = 0;
    for (int i = 0; i < line_count; i++) {
        total_size += strlen(line_batch[i]);
    }

    char* uncompressed_buffer = malloc(total_size + 1);
    if (!uncompressed_buffer) {
        perror("Failed to allocate memory for uncompressed batch");
        return false;
    }

    char* current_pos = uncompressed_buffer;
    for (int i = 0; i < line_count; i++) {
        size_t line_len = strlen(line_batch[i]);
        memcpy(current_pos, line_batch[i], line_len);
        current_pos += line_len;
    }
    *current_pos = '\0';

    // 2. Gzip the buffer
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        fprintf(stderr, "deflateInit2 failed\n");
        free(uncompressed_buffer);
        return false;
    }

    zs.avail_in = total_size;
    zs.next_in = (Bytef*)uncompressed_buffer;

    size_t compressed_buffer_size = deflateBound(&zs, total_size);
    void* compressed_buffer = malloc(compressed_buffer_size);
    if (!compressed_buffer) {
        perror("Failed to allocate memory for compressed batch");
        free(uncompressed_buffer);
        deflateEnd(&zs);
        return false;
    }

    zs.avail_out = compressed_buffer_size;
    zs.next_out = (Bytef*)compressed_buffer;

    if (deflate(&zs, Z_FINISH) != Z_STREAM_END) {
        fprintf(stderr, "deflate failed\n");
        free(uncompressed_buffer);
        free(compressed_buffer);
        deflateEnd(&zs);
        return false;
    }

    size_t compressed_size = zs.total_out;
    deflateEnd(&zs);
    free(uncompressed_buffer);

    // 3. Send the compressed data via the callback
    printf("Sending batch of %d lines (compressed size: %zu bytes)...\n", line_count, compressed_size);
    bool success = send_func(compressed_buffer, compressed_size, user_context);

    free(compressed_buffer);
    return success;
}

void offline_queue_process(send_batch_func_t send_func, void* user_context) {
    if (!send_func) return;

    FILE* infile = fopen(g_log_file_path, "r");
    if (!infile) return; // No file to process

    fseek(infile, 0, SEEK_END);
    if (ftell(infile) == 0) {
        fclose(infile);
        return; // File is empty
    }
    fseek(infile, 0, SEEK_SET);

    printf("Processing offline data queue...\n");

    FILE* tmpfile = fopen(g_temp_log_file_path, "w");
    if (!tmpfile) {
        perror("Could not open temp file for offline queue processing");
        fclose(infile);
        return;
    }

    char line[MAX_LINE_LENGTH];
    char* line_batch[MAX_BATCH_SIZE];
    int line_count = 0;
    bool any_batch_failed = false;

    while (fgets(line, sizeof(line), infile)) {
        // Strip newline characters if they exist
        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) == 0) continue; // Skip empty lines

        // Add a newline to the end of the line before batching
        size_t line_len = strlen(line);
        char* line_with_newline = malloc(line_len + 2);
        if (!line_with_newline) {
            perror("malloc failed for line");
            any_batch_failed = true;
            break;
        }
        snprintf(line_with_newline, line_len + 2, "%s\n", line);

        line_batch[line_count] = line_with_newline;
        line_count++;

        if (line_count == MAX_BATCH_SIZE) {
            bool success = process_batch(send_func, user_context, line_batch, line_count);
            if (!success) {
                any_batch_failed = true;
                for (int i = 0; i < line_count; i++) {
                    fputs(line_batch[i], tmpfile);
                }
            }
            for (int i = 0; i < line_count; i++) free(line_batch[i]);
            line_count = 0;
        }
    }

    // Process any remaining lines in the last batch
    if (line_count > 0) {
        bool success = process_batch(send_func, user_context, line_batch, line_count);
        if (!success) {
            any_batch_failed = true;
            for (int i = 0; i < line_count; i++) {
                fputs(line_batch[i], tmpfile);
            }
        }
        for (int i = 0; i < line_count; i++) free(line_batch[i]);
    }

    fclose(infile);
    fclose(tmpfile);

    if (any_batch_failed) {
        // If any batch failed, the original log is replaced by the temp file
        // which contains only the lines from failed batches.
        remove(g_log_file_path);
        rename(g_temp_log_file_path, g_log_file_path);
        printf("Offline queue processing finished with failures. Remaining data saved.\n");
    } else {
        // If all batches succeeded, both files are removed.
        remove(g_log_file_path);
        remove(g_temp_log_file_path);
        printf("Offline queue fully processed and sent successfully.\n");
    }
}