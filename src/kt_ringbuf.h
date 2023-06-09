#ifndef KT_RINGBUF_H
#define KT_RINGBUF_H

#include "kt_common.h"
#include "kt_alloc.h"

struct kt_headtail
{
    volatile u32 head;
    volatile u32 tail;
};

struct kt_ringbuf
{
#define INSANE_QUEUE_NAMESIZE 32
    char name[INSANE_QUEUE_NAMESIZE] _kt_cache_aligned;

    void *data; /**< Data buffer. */

    u32 size;  /**< Size of ring. */
    u32 esize; /**< Size of each element. */
    u32 mask;  /**< Mask (size-1) of ring. */
    u32 capacity;

    // char pad0 _kt_cache_aligned;

    struct kt_headtail prod;
    // char pad1 _kt_cache_aligned;
    struct kt_headtail cons;
    // char pad2 _kt_cache_aligned;
};

/**
 * @brief Create a new Ring Buffer
 *
 * @param name Ring Buffer name
 * @param count Size of the ring buffer
 * @param esize Size of the elements
 * @return struct kt_ringbuf*
 */
struct kt_ringbuf *kt_ringbuf_create(struct kt_allocator *al, const char *name, u32 count, u32 esize);

/**
 * @brief Get the capacity of the ring buffer
 *
 * @param rb Pointer to the ring buffer
 * @return u32 Capacity of the ring buffer
 */
u32 kt_ringbuf_get_capacity(const struct kt_ringbuf *rb);

/**
 * @brief Return the number of entries in a ring.
 *
 * @param rb Pointer to the ring buffer
 * @return u32 Number of entries in the ring buffer
 */
u32 kt_ringbuf_count(const struct kt_ringbuf *rb);

enum kt_ringbuf_behavior
{
    KT_RB_BEHAVIOR_FIXED = 0,
    KT_RB_BEHAVIOR_VARIABLE = 1,
};

/**
 * @brief Moves the producer head of a ring buffer by n units.
 *
 * @param rb Pointer to the ring buffer.
 * @param n Number of elements to move.
 * @param old_head Previous producer head value.
 * @param new_head New producer head value.
 * @param free_entries Free entries in the ring buffer after moving.
 *
 * @return The number of elements moved.
 */
static inline u32 __kt_ringbuf_move_prod_head(struct kt_ringbuf *rb, u32 n, enum kt_ringbuf_behavior behavior,
                                              u32 *old_head, u32 *new_head, u32 *free_entries);

static inline void __kt_ringbuf_enqueue_elems(struct kt_ringbuf *rb, u32 prod_head, const void *obj_table,
                                              u32 esize, u32 n);

static inline u32 __kt_ringbuf_do_enqueue_elems(struct kt_ringbuf *rb, const void *obj_table, u32 esize, u32 n,
                                                enum kt_ringbuf_behavior behavior, u32 *free_space);

u32 kt_ringbuf_enqueue_burst(struct kt_ringbuf *rb, const void *obj_table, u32 esize, u32 n, u32 *free_space);

static inline u32 __kt_ringbuf_move_cons_head(struct kt_ringbuf *rb, u32 n, enum kt_ringbuf_behavior behavior,
                                              u32 *old_head, u32 *new_head, u32 *entries);

static inline void __kt_ringbuf_dequeue_elems(struct kt_ringbuf *rb, u32 cons_head, void *obj_table, u32 esize,
                                              u32 n);

static inline u32 __kt_ringbuf_do_dequeue_elems(struct kt_ringbuf *rb, void *obj_table, u32 esize, u32 n,
                                                enum kt_ringbuf_behavior behavior, u32 *available);

u32 kt_ringbuf_dequeue_burst(struct kt_ringbuf *rb, void *obj_table, u32 esize, u32 n, u32 *available);

#endif // KT_RINGBUF_H