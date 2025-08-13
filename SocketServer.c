#include "SocketServer.h"
#include "Measurement.h"
#include "DataPublisher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <math.h> // For isnan

#define SERVER_PORT 2025 // Port for the server to listen on
#define MAX_CLIENTS 5
#define JSON_BUFFER_SIZE 2048

// Thread function to handle communication with a single client.
void* handle_client_thread(void* client_socket_ptr) {
    int client_socket = *(int*)client_socket_ptr;
    free(client_socket_ptr); // Free the allocated memory for the socket descriptor

    char json_buffer[JSON_BUFFER_SIZE];

    while (1) {
        // For now, create dummy data since we don't have shared state integration
        // TODO: Integrate with ApplicationManager to get real data
        Channel local_channels[NUM_CHANNELS];
        GPSData local_gps_data = {0};
        
        // Initialize dummy data
        for (int i = 0; i < NUM_CHANNELS; i++) {
            channel_init(&local_channels[i]);
            snprintf(local_channels[i].id, sizeof(local_channels[i].id), "channel_%d", i);
            local_channels[i].raw_adc_value = 1000 + i * 100; // Dummy ADC values
            local_channels[i].is_active = true;
        }

        // Format the data into a JSON string
        int offset = snprintf(json_buffer, JSON_BUFFER_SIZE, "{\"timestamp\": %ld, \"measurements\": [", time(NULL));
        for (int i = 0; i < NUM_CHANNELS; i++) {
            offset += snprintf(json_buffer + offset, JSON_BUFFER_SIZE - offset,
                "{\"id\": \"%s\", \"adc\": %d, \"value\": %.4f}%s",
                local_channels[i].id,
                local_channels[i].raw_adc_value,
                channel_get_calibrated_value(&local_channels[i]),
                (i == NUM_CHANNELS - 1) ? "" : ",");
        }
        offset += snprintf(json_buffer + offset, JSON_BUFFER_SIZE - offset, "], \"gps\": {");

        // Add GPS data, handling NaN values for valid JSON
        if (!isnan(local_gps_data.latitude)) {
            offset += snprintf(json_buffer + offset, JSON_BUFFER_SIZE - offset, "\"latitude\": %.6f,", local_gps_data.latitude);
        }
        if (!isnan(local_gps_data.longitude)) {
            offset += snprintf(json_buffer + offset, JSON_BUFFER_SIZE - offset, "\"longitude\": %.6f,", local_gps_data.longitude);
        }
        if (!isnan(local_gps_data.altitude)) {
            offset += snprintf(json_buffer + offset, JSON_BUFFER_SIZE - offset, "\"altitude\": %.2f,", local_gps_data.altitude);
        }
        if (!isnan(local_gps_data.speed)) {
             offset += snprintf(json_buffer + offset, JSON_BUFFER_SIZE - offset, "\"speed\": %.2f", local_gps_data.speed);
        }
        // Remove trailing comma if any GPS data was added
        if (json_buffer[offset-1] == ',') {
            json_buffer[offset-1] = '\0';
        }
        
        snprintf(json_buffer + offset, JSON_BUFFER_SIZE - offset, "}}\n"); // End of JSON object with newline

        // Send the JSON data to the client
        if (send(client_socket, json_buffer, strlen(json_buffer), 0) < 0) {
            perror("Socket send failed");
            break; // Exit loop on send error
        }

        usleep(500 * 1000); // Sleep for 500 milliseconds before sending the next update
    }

    printf("Client disconnected.\n");
    close(client_socket);
    return NULL;
}


void* socket_server_thread_func(void* arg) {
    int server_fd, client_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        return NULL;
    }

    // Forcefully attaching socket to the port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        close(server_fd);
        return NULL;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT);

    // Binding the socket to the network address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Socket bind failed");
        close(server_fd);
        return NULL;
    }

    // Start listening for incoming connections
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Socket listen failed");
        close(server_fd);
        return NULL;
    }

    printf("Socket server listening on port %d\n", SERVER_PORT);

    while (1) {
        // Accept a new client connection
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Socket accept failed");
            continue; // Continue to listen for other clients
        }

        printf("New client connected.\n");

        // Create a new thread to handle this client
        pthread_t client_thread;
        int* new_sock = malloc(sizeof(int));
        *new_sock = client_socket;
        if (pthread_create(&client_thread, NULL, handle_client_thread, (void*)new_sock) != 0) {
            perror("Failed to create client handler thread");
            close(client_socket);
            free(new_sock);
        }
        pthread_detach(client_thread); // Detach the thread so we don't have to join it
    }

    close(server_fd);
    return NULL;
}
