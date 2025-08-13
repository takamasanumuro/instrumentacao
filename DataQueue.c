#include "DataQueue.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

// Node for the linked list queue
typedef struct DataNode {
    char* data;
    struct DataNode* next;
} DataNode;

// Thread-safe queue structure
struct DataQueue {
    DataNode* head;
    DataNode* tail;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    volatile int shutdown; // Flag to signal threads to exit
};

/**
 * @brief Creates and initializes a new thread-safe data queue.
 * @return A pointer to the new DataQueue, or NULL on failure.
 */
DataQueue* data_queue_create() {
    DataQueue* q = (DataQueue*)malloc(sizeof(DataQueue));
    if (!q) {
        perror("Failed to allocate DataQueue");
        return NULL;
    }
    q->head = NULL;
    q->tail = NULL;
    q->shutdown = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
    return q;
}

/**
 * @brief Destroys a data queue and frees all its resources.
 * @param q The queue to destroy.
 */
void data_queue_destroy(DataQueue* q) {
    if (!q) return;
    // Free any remaining nodes in the list
    DataNode* current = q->head;
    while (current != NULL) {
        DataNode* next = current->next;
        free(current->data); // Free the string copy
        free(current);       // Free the node itself
        current = next;
    }
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
    free(q);
}

/**
 * @brief Enqueues a data item.
 *
 * Makes a copy of the data string and adds it to the tail of the queue.
 * @param q The queue.
 * @param data The null-terminated string data to enqueue.
 */
void data_queue_enqueue(DataQueue* q, const char* data) {
    DataNode* new_node = (DataNode*)malloc(sizeof(DataNode));
    if (!new_node) {
        perror("Failed to allocate DataNode");
        return;
    }
    // strdup allocates memory for the copy and copies the string
    new_node->data = strdup(data);
    if (!new_node->data) {
        perror("Failed to duplicate string for queue");
        free(new_node);
        return;
    }
    new_node->next = NULL;

    pthread_mutex_lock(&q->mutex);
    if (q->tail != NULL) {
        q->tail->next = new_node;
    }
    q->tail = new_node;
    if (q->head == NULL) {
        q->head = new_node;
    }
    // Signal the condition variable in case the dequeue thread is waiting
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

/**
 * @brief Dequeues a data item.
 *
 * Blocks until an item is available or the queue is shut down.
 * The caller is responsible for freeing the returned string.
 * @param q The queue.
 * @return A pointer to the data string, or NULL if the queue is empty and has been shut down.
 */
char* data_queue_dequeue(DataQueue* q) {
    pthread_mutex_lock(&q->mutex);
    // Wait while the queue is empty and not in shutdown mode
    while (q->head == NULL && !q->shutdown) {
        pthread_cond_wait(&q->cond, &q->mutex);
    }

    // If we woke up due to shutdown and the queue is empty, return NULL
    if (q->shutdown && q->head == NULL) {
        pthread_mutex_unlock(&q->mutex);
        return NULL;
    }

    // Dequeue the head item
    DataNode* temp = q->head;
    char* data = temp->data;
    q->head = q->head->next;
    if (q->head == NULL) {
        q->tail = NULL; // The queue is now empty
    }
    free(temp); // Free the node, but not the data it points to

    pthread_mutex_unlock(&q->mutex);
    return data;
}

/**
 * @brief Signals the queue to shut down.
 *
 * This will cause any threads blocked in dequeue to wake up.
 * @param q The queue.
 */
void data_queue_shutdown(DataQueue* q) {
    pthread_mutex_lock(&q->mutex);
    q->shutdown = 1;
    // Broadcast to wake up all waiting threads
    pthread_cond_broadcast(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}
