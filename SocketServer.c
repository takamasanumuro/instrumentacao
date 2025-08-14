#include "SocketServer.h"
#include "Channel.h"
#include "HardwareManager.h"  // For GPSData
#include "ApplicationManager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <string.h>  // For memset

#define MAX_CLIENTS 5
#define JSON_BUFFER_SIZE 4096  // Increased buffer size for safety
#define CLIENT_TIMEOUT_SECONDS 30

// Client connection context
typedef struct {
    int socket;
    SocketServerContext* server_ctx;
    pthread_t thread;
    time_t last_activity;
} ClientContext;

// Forward declarations
static void* server_thread_func(void* arg);
static void* client_thread_func(void* arg);
static int create_json_response(char* buffer, size_t buffer_size, 
                               const Channel* channels, const GPSData* gps_data);
static bool is_valid_json_char(char c);
static void safe_json_escape(const char* input, char* output, size_t output_size);

SocketServerContext* socket_server_create(HardwareManager* hardware_manager, YAMLAppConfig* config) {
    if (!hardware_manager || !config) {
        fprintf(stderr, "SocketServer: Invalid parameters\n");
        return NULL;
    }
    
    if (!config->network.socket_server_enabled) {
        printf("SocketServer: Disabled in configuration\n");
        return NULL;
    }

    SocketServerContext* ctx = calloc(1, sizeof(SocketServerContext));
    if (!ctx) {
        fprintf(stderr, "SocketServer: Memory allocation failed\n");
        return NULL;
    }

    ctx->hardware_manager = hardware_manager;
    ctx->config = config;
    ctx->running = false;
    ctx->shutdown_requested = false;

    return ctx;
}

bool socket_server_start(SocketServerContext* ctx) {
    if (!ctx || ctx->running) {
        return false;
    }

    printf("SocketServer: Starting server on port %d\n", ctx->config->network.socket_port);

    if (pthread_create(&ctx->server_thread, NULL, server_thread_func, ctx) != 0) {
        fprintf(stderr, "SocketServer: Failed to create server thread: %s\n", strerror(errno));
        return false;
    }

    ctx->running = true;
    return true;
}

void socket_server_shutdown(SocketServerContext* ctx) {
    if (!ctx || !ctx->running) {
        return;
    }

    printf("SocketServer: Shutdown requested\n");
    ctx->shutdown_requested = true;
}

void socket_server_destroy(SocketServerContext* ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->running) {
        socket_server_shutdown(ctx);
        pthread_join(ctx->server_thread, NULL);
    }

    printf("SocketServer: Server stopped\n");
    free(ctx);
}

static void* server_thread_func(void* arg) {
    SocketServerContext* ctx = (SocketServerContext*)arg;
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        fprintf(stderr, "SocketServer: Socket creation failed: %s\n", strerror(errno));
        ctx->running = false;
        return NULL;
    }

    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        fprintf(stderr, "SocketServer: setsockopt failed: %s\n", strerror(errno));
        close(server_fd);
        ctx->running = false;
        return NULL;
    }

    // Configure address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(ctx->config->network.socket_port);

    // Bind socket
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        fprintf(stderr, "SocketServer: Bind failed: %s\n", strerror(errno));
        close(server_fd);
        ctx->running = false;
        return NULL;
    }

    // Listen for connections
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        fprintf(stderr, "SocketServer: Listen failed: %s\n", strerror(errno));
        close(server_fd);
        ctx->running = false;
        return NULL;
    }

    printf("SocketServer: Listening on port %d\n", ctx->config->network.socket_port);

    while (!ctx->shutdown_requested) {
        // Accept new client
        int client_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        
        if (client_socket < 0) {
            if (ctx->shutdown_requested) {
                break;
            }
            fprintf(stderr, "SocketServer: Accept failed: %s\n", strerror(errno));
            continue;
        }

        printf("SocketServer: New client connected (socket %d)\n", client_socket);

        // Create client context
        ClientContext* client_ctx = malloc(sizeof(ClientContext));
        if (!client_ctx) {
            fprintf(stderr, "SocketServer: Failed to allocate client context\n");
            close(client_socket);
            continue;
        }

        client_ctx->socket = client_socket;
        client_ctx->server_ctx = ctx;
        client_ctx->last_activity = time(NULL);

        // Create client handler thread
        if (pthread_create(&client_ctx->thread, NULL, client_thread_func, client_ctx) != 0) {
            fprintf(stderr, "SocketServer: Failed to create client thread: %s\n", strerror(errno));
            close(client_socket);
            free(client_ctx);
            continue;
        }

        pthread_detach(client_ctx->thread);
    }

    close(server_fd);
    ctx->running = false;
    printf("SocketServer: Server thread exiting\n");
    return NULL;
}

static void* client_thread_func(void* arg) {
    ClientContext* client_ctx = (ClientContext*)arg;
    SocketServerContext* server_ctx = client_ctx->server_ctx;
    char json_buffer[JSON_BUFFER_SIZE];
    Channel channels[NUM_CHANNELS];
    GPSData gps_data;

    // Get update interval from configuration (default 500ms if not configured)
    int update_interval_ms = server_ctx->config->network.update_interval_ms > 0 ? 
                            server_ctx->config->network.update_interval_ms : 500;

    while (!server_ctx->shutdown_requested) {
        // Check for client timeout
        time_t now = time(NULL);
        if (now - client_ctx->last_activity > CLIENT_TIMEOUT_SECONDS) {
            printf("SocketServer: Client timeout (socket %d)\n", client_ctx->socket);
            break;
        }

        // Get current data from HardwareManager
        const Channel* hw_channels = hardware_manager_get_channels(server_ctx->hardware_manager);
        bool gps_success = hardware_manager_get_current_gps(server_ctx->hardware_manager, &gps_data);
        
        if (!hw_channels) {
            fprintf(stderr, "SocketServer: Failed to get channel data from hardware manager\n");
            usleep(update_interval_ms * 1000);
            continue;
        }
        
        // Copy channels for JSON generation (create_json_response expects non-const)
        for (int i = 0; i < NUM_CHANNELS && i < hardware_manager_get_channel_count(server_ctx->hardware_manager); i++) {
            channels[i] = hw_channels[i];
        }
        
        // Use empty GPS data if GPS is not available
        if (!gps_success) {
            memset(&gps_data, 0, sizeof(GPSData));
            gps_data.latitude = NAN;
            gps_data.longitude = NAN;
            gps_data.altitude = NAN;
            gps_data.speed = NAN;
        }

        // Create JSON response
        int json_len = create_json_response(json_buffer, JSON_BUFFER_SIZE, channels, &gps_data);
        if (json_len <= 0) {
            fprintf(stderr, "SocketServer: Failed to create JSON response\n");
            break;
        }

        // Send response to client
        ssize_t sent = send(client_ctx->socket, json_buffer, json_len, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EPIPE || errno == ECONNRESET) {
                printf("SocketServer: Client disconnected (socket %d)\n", client_ctx->socket);
            } else {
                fprintf(stderr, "SocketServer: Send failed: %s\n", strerror(errno));
            }
            break;
        }

        client_ctx->last_activity = now;
        usleep(update_interval_ms * 1000);
    }

    printf("SocketServer: Client handler exiting (socket %d)\n", client_ctx->socket);
    close(client_ctx->socket);
    free(client_ctx);
    return NULL;
}

static int create_json_response(char* buffer, size_t buffer_size, 
                               const Channel* channels, const GPSData* gps_data) {
    if (!buffer || buffer_size < 512) {
        return -1;
    }

    char escaped_id[64];
    char escaped_unit[32];
    size_t offset = 0;
    time_t timestamp = time(NULL);

    // Start JSON object
    int written = snprintf(buffer + offset, buffer_size - offset,
        "{\"timestamp\":%ld,\"measurements\":[", timestamp);
    if (written < 0 || (size_t)written >= buffer_size - offset) return -1;
    offset += written;

    // Add channel measurements
    bool first_channel = true;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (!channels[i].is_active) {
            continue;
        }

        if (!first_channel) {
            if (offset + 1 >= buffer_size) return -1;
            buffer[offset++] = ',';
        }

        // Safely escape strings
        safe_json_escape(channels[i].id, escaped_id, sizeof(escaped_id));
        safe_json_escape(channels[i].unit, escaped_unit, sizeof(escaped_unit));

        written = snprintf(buffer + offset, buffer_size - offset,
            "{\"id\":\"%s\",\"pin\":%d,\"adc\":%d,\"value\":%.6f,\"unit\":\"%s\"}",
            escaped_id,
            channels[i].pin,
            channels[i].raw_adc_value,
            channel_get_calibrated_value(&channels[i]),
            escaped_unit);

        if (written < 0 || (size_t)written >= buffer_size - offset) return -1;
        offset += written;
        first_channel = false;
    }

    // Add GPS data
    written = snprintf(buffer + offset, buffer_size - offset, "],\"gps\":{");
    if (written < 0 || (size_t)written >= buffer_size - offset) return -1;
    offset += written;

    bool first_gps_field = true;
    
    if (!isnan(gps_data->latitude)) {
        written = snprintf(buffer + offset, buffer_size - offset,
            "\"latitude\":%.8f", gps_data->latitude);
        if (written < 0 || (size_t)written >= buffer_size - offset) return -1;
        offset += written;
        first_gps_field = false;
    }

    if (!isnan(gps_data->longitude)) {
        if (!first_gps_field) {
            if (offset + 1 >= buffer_size) return -1;
            buffer[offset++] = ',';
        }
        written = snprintf(buffer + offset, buffer_size - offset,
            "\"longitude\":%.8f", gps_data->longitude);
        if (written < 0 || (size_t)written >= buffer_size - offset) return -1;
        offset += written;
        first_gps_field = false;
    }

    if (!isnan(gps_data->altitude)) {
        if (!first_gps_field) {
            if (offset + 1 >= buffer_size) return -1;
            buffer[offset++] = ',';
        }
        written = snprintf(buffer + offset, buffer_size - offset,
            "\"altitude\":%.2f", gps_data->altitude);
        if (written < 0 || (size_t)written >= buffer_size - offset) return -1;
        offset += written;
        first_gps_field = false;
    }

    if (!isnan(gps_data->speed)) {
        if (!first_gps_field) {
            if (offset + 1 >= buffer_size) return -1;
            buffer[offset++] = ',';
        }
        written = snprintf(buffer + offset, buffer_size - offset,
            "\"speed\":%.2f", gps_data->speed);
        if (written < 0 || (size_t)written >= buffer_size - offset) return -1;
        offset += written;
    }

    // Close JSON object
    written = snprintf(buffer + offset, buffer_size - offset, "}}\n");
    if (written < 0 || (size_t)written >= buffer_size - offset) return -1;
    offset += written;

    return (int)offset;
}

static bool is_valid_json_char(char c) {
    return c >= 32 && c != '"' && c != '\\';
}

static void safe_json_escape(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size < 2) {
        if (output && output_size > 0) output[0] = '\0';
        return;
    }

    size_t out_pos = 0;
    size_t in_len = strlen(input);
    
    for (size_t i = 0; i < in_len && out_pos < output_size - 1; i++) {
        char c = input[i];
        
        if (is_valid_json_char(c)) {
            output[out_pos++] = c;
        } else if (c == '"' || c == '\\') {
            if (out_pos < output_size - 2) {
                output[out_pos++] = '\\';
                output[out_pos++] = c;
            } else {
                break;
            }
        }
        // Skip invalid characters
    }
    
    output[out_pos] = '\0';
}

// Legacy function for backward compatibility
void* socket_server_thread_func(void* arg) {
    return server_thread_func(arg);
}