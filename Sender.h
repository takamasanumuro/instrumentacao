#ifndef SENDER_H
#define SENDER_H

#include "ConfigYAML.h"

// Opaque handle to the sender module
typedef struct SenderContext SenderContext;

/**
 * @brief Creates and initializes the sender module using configuration from environment variables
 *
 * This function sets up the sending queue, and starts the background thread(s)
 * for sending data and processing the offline queue.
 *
 * @return A pointer to the SenderContext on success, NULL on failure.
 */
SenderContext* sender_create_from_env(void);

/**
 * @brief Creates and initializes the sender module using YAML configuration
 *
 * This function sets up the sending queue using the provided YAML configuration,
 * and starts the background thread(s) for sending data and processing the offline queue.
 *
 * @param config The YAML configuration containing InfluxDB settings
 * @return A pointer to the SenderContext on success, NULL on failure.
 */
SenderContext* sender_create_from_yaml(const YAMLAppConfig* config);

/**
 * @brief Destroys the sender module and cleans up its resources.
 *
 * This function signals the sender threads to shut down, waits for them to complete,
 * and then frees all associated resources.
 *
 * @param context The sender context to destroy.
 */
void sender_destroy(SenderContext* context);

/**
 * @brief Submits a measurement string to the sending queue.
 *
 * This function is the main interface for other threads to send data. It adds the
 * data to a thread-safe queue to be processed by the sender thread.
 * This function is non-blocking and makes a copy of the provided data.
 *
 * @param context The sender context.
 * @param line_protocol The null-terminated string (in line protocol format) to be sent.
 */
void sender_submit(SenderContext* context, const char* line_protocol);

#endif // SENDER_H