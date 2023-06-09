#ifndef KT_QUEUE_H
#define KT_QUEUE_H

#include "kt_common.h"

struct kt_pq_node
{
    i64 prio;
    void *data;
};

/**
 * @brief A priority queue implemented as a min heap.
 */
struct kt_prio_queue
{
    struct kt_pq_node *elems; // Pointer to the array of elements in the heap

    size_t cap; // Maximum capacity of the heap
    size_t size; // Current size of the heap
};

/**
 * Initializes a new priority queue with the given capacity.
 *
 * @param cap The maximum capacity of the priority queue.
 * @return A new priority queue.
 */
struct kt_prio_queue kt_prio_queue_init(size_t cap);

/**
 * @brief Checks if the priority queue is empty.
 * 
 * @param q The priority queue.
 * @return 1 if the priority queue is empty, 0 otherwise. 
 */
int kt_prio_queue_is_empty(struct kt_prio_queue *q);

/**
 * @brief Inserts a new element into the priority queue.
 *
 * @param q The priority queue.
 * @param prio The priority of the element.
 * @param data The data of the element.
 * @return 0 if the element was successfully inserted, -1 if the priority queue is full.
 */
int kt_prio_queue_insert(struct kt_prio_queue *q, i64 prio, void *data);

/**
 * Decreases the timestamp of an element in the priority queue.
 *
 * @param h The priority queue.
 * @param i The index of the element to update.
 * @param new_val The new timestamp of the element.
 */
void kt_prio_queue_decrease_key(struct kt_prio_queue *q, size_t i, i64 new_val);

/**
 * Returns the minimum timestamp in the priority queue.
 *
 * @param h The priority queue.
 * @return The minimum timestamp in the priority queue.
 */
i64 kt_prio_queue_getmin(struct kt_prio_queue *q);

/**
 * Extracts the minimum element from the priority queue.
 *
 * @param q The priority queue.
 * @param data The data of the minimum element.
 * @return 0 if the element was successfully extracted, -1 if the priority queue is empty.
 */
int kt_prio_queue_extract_min(struct kt_prio_queue *q, void *data);

/**
 * Deletes an element from the priority queue.
 *
 * @param q The priority queue.
 * @param i The index of the element to delete.
 */
void kt_prio_queue_delete_key(struct kt_prio_queue *q, size_t i);

#endif // KT_QUEUE_H