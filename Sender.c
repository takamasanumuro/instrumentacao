#include "Sender.h"
#include "DataQueue.h"
#include "OfflineQueue.h"
#include "util.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 
#include <curl/curl.h>

typedef struct _InfluxDBContext {
    char *url;
    char *bucket;
    char *org;
    char *token;
} InfluxDBContext;

#define OFFLINE_QUEUE_PROCESS_INTERVAL_S 60

// The full definition of the SenderContext is here, making it opaque.
struct SenderContext {
    DataQueue* queue;
    pthread_t sender_thread_id;
    pthread_t offline_processor_thread_id;
    volatile bool is_running;
    InfluxDBContext influxdb_context;
};

// --- Private Function Prototypes ---
static bool send_http_post(const SenderContext* context, const char* url, struct curl_slist* headers, const void* post_data, long post_size);
static bool send_line_protocol(SenderContext* context, const char* line_protocol);
static bool send_compressed_batch_callback(const void* data, size_t size, void* user_context);
static void* sender_thread_function(void* arg);
static void* offline_processor_thread_function(void* arg);

// --- Public Functions ---

SenderContext* sender_create_from_env() {
    SenderContext* context = calloc(1, sizeof(SenderContext));
    if (!context) {
        perror("Failed to allocate memory for SenderContext");
        return NULL;
    }

    context->influxdb_context.url = getenv("INFLUXDB_URL");
    context->influxdb_context.bucket = getenv("INFLUXDB_BUCKET");
    context->influxdb_context.org = getenv("INFLUXDB_ORG");
    context->influxdb_context.token = getenv("INFLUXDB_TOKEN");

    if (!context->influxdb_context.url || !context->influxdb_context.bucket || !context->influxdb_context.org || !context->influxdb_context.token) {
        fprintf(stderr, "Failed to get InfluxDB configuration from environment variables.\n");
        free(context);
        return NULL;
    }

    context->queue = data_queue_create();
    if (!context->queue) {
        fprintf(stderr, "Failed to create sender queue.\n");
        free(context);
        return NULL;
    }

    offline_queue_init("logs/offline_log.txt");

    context->is_running = true;

    if (pthread_create(&context->sender_thread_id, NULL, sender_thread_function, context) != 0) {
        perror("Failed to create sender thread");
        data_queue_destroy(context->queue);
        free(context);
        return NULL;
    }

    if (pthread_create(&context->offline_processor_thread_id, NULL, offline_processor_thread_function, context) != 0) {
        perror("Failed to create offline processor thread");
        // Stop the already running sender thread
        context->is_running = false;
        data_queue_shutdown(context->queue);
        pthread_join(context->sender_thread_id, NULL);
        data_queue_destroy(context->queue);
        free(context);
        return NULL;
    }

    return context;
}

SenderContext* sender_create_from_yaml(const YAMLAppConfig* config) {
    if (!config) {
        fprintf(stderr, "NULL YAML configuration provided to sender_create_from_yaml\n");
        return NULL;
    }
    
    SenderContext* context = calloc(1, sizeof(SenderContext));
    if (!context) {
        perror("Failed to allocate memory for SenderContext");
        return NULL;
    }

    // Set InfluxDB configuration from YAML
    context->influxdb_context.url = config->influxdb.url;
    context->influxdb_context.bucket = config->influxdb.bucket;
    context->influxdb_context.org = config->influxdb.org;
    context->influxdb_context.token = config->influxdb.token;

    // Validate configuration
    if (!context->influxdb_context.url || strlen(context->influxdb_context.url) == 0 ||
        !context->influxdb_context.bucket || strlen(context->influxdb_context.bucket) == 0 ||
        !context->influxdb_context.org || strlen(context->influxdb_context.org) == 0 ||
        !context->influxdb_context.token || strlen(context->influxdb_context.token) == 0) {
        fprintf(stderr, "Incomplete InfluxDB configuration in YAML file.\n");
        free(context);
        return NULL;
    }

    context->queue = data_queue_create();
    if (!context->queue) {
        fprintf(stderr, "Failed to create sender queue.\n");
        free(context);
        return NULL;
    }

    // Use CSV directory from YAML config for offline queue
    char offline_queue_path[512];
    snprintf(offline_queue_path, sizeof(offline_queue_path), "%s/offline_log.txt", 
             config->logging.csv_directory);
    offline_queue_init(offline_queue_path);

    context->is_running = true;

    if (pthread_create(&context->sender_thread_id, NULL, sender_thread_function, context) != 0) {
        perror("Failed to create sender thread");
        data_queue_destroy(context->queue);
        free(context);
        return NULL;
    }

    if (pthread_create(&context->offline_processor_thread_id, NULL, offline_processor_thread_function, context) != 0) {
        perror("Failed to create offline processor thread");
        // Stop the already running sender thread
        context->is_running = false;
        data_queue_shutdown(context->queue);
        pthread_join(context->sender_thread_id, NULL);
        data_queue_destroy(context->queue);
        free(context);
        return NULL;
    }

    printf("Sender module initialized with YAML configuration:\n");
    printf("  - InfluxDB URL: %s\n", context->influxdb_context.url);
    printf("  - Bucket: %s\n", context->influxdb_context.bucket);
    printf("  - Organization: %s\n", context->influxdb_context.org);
    printf("  - Offline queue: %s\n", offline_queue_path);

    return context;
}

void sender_destroy(SenderContext* context) {
    if (!context || !context->is_running) {
        return;
    }
    printf("Stopping sender module...\n");
    context->is_running = false;

    // Signal the queue to shut down, waking up the sender thread if it's waiting
    data_queue_shutdown(context->queue);

    // Wait for the threads to finish
    pthread_join(context->sender_thread_id, NULL);
    pthread_join(context->offline_processor_thread_id, NULL);

    // Clean up resources
    data_queue_destroy(context->queue);
    free(context);
    printf("Sender module stopped.\n");
}

void sender_submit(SenderContext* context, const char* line_protocol) {
    if (!context || !context->is_running) {
        fprintf(stderr, "Cannot submit measurement, sender is not running.\n");
        offline_queue_add(line_protocol); // Fallback to offline queue
        return;
    }
    data_queue_enqueue(context->queue, line_protocol);
}

// --- Private Function Implementations ---

static void* sender_thread_function(void* arg) {
    SenderContext* context = (SenderContext*)arg;
    printf("Sender thread started.\n");

    while (context->is_running) {
        char* data_to_send = data_queue_dequeue(context->queue);
        if (data_to_send == NULL) { // This happens on shutdown
            if (!context->is_running) break;
            continue;
        }

        if (!send_line_protocol(context, data_to_send)) {
            fprintf(stderr, "Sender: Failed to send data, queuing to offline file.\n");
            offline_queue_add(data_to_send);
        }

        free(data_to_send);
    }

    printf("Sender thread finished.\n");
    return NULL;
}

static void* offline_processor_thread_function(void* arg) {
    SenderContext* context = (SenderContext*)arg;
    printf("Offline queue processor thread started.\n");

    while (context->is_running) {
        for (int i = 0; i < OFFLINE_QUEUE_PROCESS_INTERVAL_S && context->is_running; ++i) {
            sleep(1);
        }

        if (context->is_running) {
            offline_queue_process(send_compressed_batch_callback, context);
        }
    }

    printf("Offline queue processor thread finished.\n");
    return NULL;
}

// This is the callback that the OfflineQueue will use to send data.
// It delegates to the private send_http_post function.
static bool send_compressed_batch_callback(const void* data, size_t size, void* user_context) {
    SenderContext* context = (SenderContext*)user_context;
    
    char url[256];
    snprintf(url, sizeof(url), "%s/api/v2/write?org=%s&bucket=%s&precision=s",
             context->influxdb_context.url,
             context->influxdb_context.org, context->influxdb_context.bucket);

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Token %s", context->influxdb_context.token);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: text/plain; charset=utf-8");
    headers = curl_slist_append(headers, "Content-Encoding: gzip");

    bool success = send_http_post(context, url, headers, data, (long)size);

    curl_slist_free_all(headers);
    return success;
}

// A wrapper around the core curl logic for sending a single line protocol string.
static bool send_line_protocol(SenderContext* context, const char* line_protocol) {
    char url[256];
    snprintf(url, sizeof(url), "%s/api/v2/write?org=%s&bucket=%s&precision=s",
             context->influxdb_context.url,
             context->influxdb_context.org, context->influxdb_context.bucket);

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Token %s", context->influxdb_context.token);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: text/plain; charset=utf-8");

    bool success = send_http_post(context, url, headers, line_protocol, 0); // 0 post_size for null-terminated string

    curl_slist_free_all(headers);
    return success;
}

// The core, generic HTTP POST function using CURL.
static bool send_http_post(const SenderContext* context, const char* url, struct curl_slist* headers, const void* post_data, long post_size) {
    CURL* curl_handle = curl_easy_init();
    if (!curl_handle) {
        fprintf(stderr, "Failed to initialize CURL\n");
        return false;
    }

    struct MemoryStruct chunk = { .memory = malloc(1), .size = 0 };
    if (!chunk.memory) {
        fprintf(stderr, "Failed to allocate memory for CURL response\n");
        curl_easy_cleanup(curl_handle);
        return false;
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, post_data);
    if (post_size > 0) { // For binary data
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, post_size);
    }
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 20L);

    CURLcode result = curl_easy_perform(curl_handle);
    bool success = (result == CURLE_OK);

    if (!success) {
        fprintf(stderr, "CURL error: %s\n", curl_easy_strerror(result));
    }

    curl_easy_cleanup(curl_handle);
    free(chunk.memory);

    return success;
}