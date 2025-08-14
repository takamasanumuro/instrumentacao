#ifndef SOCKET_SERVER_H
#define SOCKET_SERVER_H

#include "ConfigYAML.h"
#include <pthread.h>
#include <stdbool.h>
#include "HardwareManager.h"

// Socket server context structure
typedef struct {
    HardwareManager* hardware_manager;  // Use HardwareManager directly instead of ApplicationManager
    YAMLAppConfig* config;
    pthread_t server_thread;
    volatile bool running;
    volatile bool shutdown_requested;
} SocketServerContext;

/**
 * @brief Creates and initializes a socket server context
 * @param hardware_manager Pointer to the HardwareManager instance for direct hardware access
 * @param config YAML configuration containing network settings
 * @return SocketServerContext pointer or NULL on failure
 */
SocketServerContext* socket_server_create(HardwareManager* hardware_manager, YAMLAppConfig* config);

/**
 * @brief Starts the socket server in a separate thread
 * @param ctx Socket server context
 * @return true on success, false on failure
 */
bool socket_server_start(SocketServerContext* ctx);

/**
 * @brief Requests graceful shutdown of the socket server
 * @param ctx Socket server context
 */
void socket_server_shutdown(SocketServerContext* ctx);

/**
 * @brief Waits for socket server thread to complete and cleans up resources
 * @param ctx Socket server context
 */
void socket_server_destroy(SocketServerContext* ctx);

// Legacy function for backward compatibility
void* socket_server_thread_func(void* arg);

#endif // SOCKET_SERVER_H


