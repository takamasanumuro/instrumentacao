#ifndef DATA_QUEUE_H
#define DATA_QUEUE_H

/**
 * @file DataQueue.h
 * @brief A simple thread-safe queue for passing string data between threads.
 *
 * This implementation uses a linked list with a mutex for thread safety and
 * a condition variable to allow the consumer thread to wait efficiently
 * for new data.
 */

typedef struct DataQueue DataQueue; // Opaque data queue type

/**
 * @brief Creates and initializes a new thread-safe data queue.
 * @return A pointer to the new DataQueue, or NULL on failure.
 */
DataQueue* data_queue_create();

/**
 * @brief Destroys a data queue, freeing all nodes and their data.
 * @param q The queue to destroy.
 */
void data_queue_destroy(DataQueue* q);

/**
 * @brief Adds a string to the end of the queue.
 *
 * This function is thread-safe. It creates a copy of the input string.
 * @param q The queue.
 * @param data The null-terminated string to add to the queue.
 */
void data_queue_enqueue(DataQueue* q, const char* data);

/**
 * @brief Removes and returns a string from the front of the queue.
 *
 * This function is thread-safe and will block until an item becomes available
 * or the queue is shut down. The caller is responsible for freeing the
 * memory of the returned string.
 *
 * @param q The queue.
 * @return A dynamically allocated string, or NULL if the queue is shutting down and empty.
 */
char* data_queue_dequeue(DataQueue* q);

/**
 * @brief Signals the queue to shut down, unblocking any waiting consumer threads.
 * @param q The queue.
 */
void data_queue_shutdown(DataQueue* q);

#endif // DATA_QUEUE_H
