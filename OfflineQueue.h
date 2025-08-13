#ifndef OFFLINE_QUEUE_H
#define OFFLINE_QUEUE_H

#include <stddef.h>
#include <stdbool.h>

// Callback function pointer type for sending a compressed batch.
// The function should return true on success and false on failure.
typedef bool (*send_batch_func_t)(const void* data, size_t size, void* user_context);

/**
 * @brief Initializes the offline queue module.
 *
 * @param log_file_path The path to the file to use for the offline log.
 */
void offline_queue_init(const char* log_file_path);

/**
 * @brief Adds a line protocol string to the offline queue file.
 *
 * @param line_protocol The null-terminated string to add.
 */
void offline_queue_add(const char* line_protocol);

/**
 * @brief Processes the offline queue, sending data in compressed batches.
 *
 * This function reads the offline log file, groups lines into batches,
 * compresses them, and calls the provided callback function to send them.
 *
 * @param send_func The callback function to use for sending a batch.
 * @param user_context A pointer to user-defined context that will be passed to the callback.
 */
void offline_queue_process(send_batch_func_t send_func, void* user_context);

#endif // OFFLINE_QUEUE_H
