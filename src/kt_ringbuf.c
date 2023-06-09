#include "kt_ringbuf.h"
#include "kt_logger.h"

struct kt_ringbuf *kt_ringbuf_create(struct kt_allocator *al, const char *name, u32 count, u32 esize)
{
    // calculate the size of the queue + metadata
    u32 qsize = sizeof(struct kt_ringbuf) + count * esize;
    // allocate use the page allocator
    struct kt_ringbuf *rb = (struct kt_ringbuf *)al->alloc(al, qsize);
    if (rb == NULL)
    {
        return NULL;
    }

    // copy the name
    strncpy(rb->name, name, INSANE_QUEUE_NAMESIZE - 1);

    // set the data pointer
    rb->data = (void *)((u8 *)rb + sizeof(struct kt_ringbuf));

    rb->size = count;
    rb->mask = count - 1;
    rb->capacity = rb->mask;
    rb->esize = esize;

    // set the head and tail to 0
    rb->prod.head = 0;
    rb->prod.tail = 0;
    rb->cons.head = 0;
    rb->cons.tail = 0;

    return rb;
}

u32 kt_ringbuf_get_capacity(const struct kt_ringbuf *rb)
{
    return rb->capacity;
}

u32 kt_ringbuf_count(const struct kt_ringbuf *rb)
{
	u32 prod_tail = rb->prod.tail;
	u32 cons_tail = rb->cons.tail;
	u32 count = (prod_tail - cons_tail) & rb->mask;
	return (count > rb->capacity) ? rb->capacity : count;
}

/**
 * @brief Moves the producer head of a queue by n units.
 *
 * @param q Pointer to the queue structure.
 * @param n Number of elements to move.
 * @param old_head Previous producer head value.
 * @param new_head New producer head value.
 * @param free_entries Free entries in the queue after moving.
 *
 * @return The number of elements moved.
 */
static inline u32
__kt_ringbuf_move_prod_head(struct kt_ringbuf *rb, u32 n, enum kt_ringbuf_behavior behavior, u32 *old_head,
                            u32 *new_head, u32 *free_entries)
{
    const u32 capacity = rb->capacity;
    u32 cons_tail;
    u32 max = n;

    int success;

    *old_head = atomic_load_explicit(&rb->prod.head, memory_order_relaxed);
    do
    {
        n = max;

        atomic_thread_fence(memory_order_acquire);

        cons_tail = atomic_load_explicit(&rb->cons.tail, memory_order_acquire);

        *free_entries = capacity - (*old_head - cons_tail);

        if (unlikely(n > *free_entries))
            n = (behavior == KT_RB_BEHAVIOR_FIXED) ? 0 : *free_entries;

        if (unlikely(n == 0))
            return 0;

        *new_head = *old_head + n;

        success = atomic_compare_exchange_weak_explicit(&rb->prod.head, old_head, *new_head,
                                                        memory_order_relaxed, memory_order_relaxed);
    } while (unlikely(success == 0));

    return n;
}

static inline void
__kt_ringbuf_enqueue_elems(struct kt_ringbuf *rb, u32 prod_head, const void *obj_table, u32 esize, u32 n)
{
    if (esize == 8)
    {
        u32 i;
        const u32 size = rb->size;
        u32 idx = prod_head & rb->mask;
        u64 *ring = (u64 *)(rb + 1);
        const u64 *src = (const u64 *)obj_table;

        if (likely(idx + n <= size))
        {
            for (i = 0; i < (n & ~0x3); i += 4, idx += 4)
            {
                ring[idx] = src[i];
                ring[idx + 1] = src[i + 1];
                ring[idx + 2] = src[i + 2];
                ring[idx + 3] = src[i + 3];
            }
            switch (n & 0x3)
            {
            case 3:
                ring[idx++] = src[i++]; // fallthrough
            case 2:
                ring[idx++] = src[i++]; // fallthrough
            case 1:
                ring[idx++] = src[i++]; // fallthrough            
            }
        }
        else
        {
            for (i = 0; i < n; i++, idx++)
            {
                ring[idx] = src[i];
            }
            for (idx = 0; i < n; i++, idx++)
            {
                ring[idx] = src[i];
            }
        }
    }
    else
    {
        assert("not implemented" == 0);
    }
}

static inline u32
__kt_ringbuf_do_enqueue_elems(struct kt_ringbuf *rb, const void *obj_table, u32 esize, u32 n,
                              enum kt_ringbuf_behavior behavior, u32 *free_space)
{
    u32 prod_head, prod_next;
    u32 free_entries;

    n = __kt_ringbuf_move_prod_head(rb, n, behavior, &prod_head, &prod_next, &free_entries);
    if (n == 0)
    {
        LOG_TRACE("no free entries in ringbuf\n");
        goto end;
    }

    // now we can enqueue entries in the ring
    __kt_ringbuf_enqueue_elems(rb, prod_head, obj_table, esize, n);

    // update producer tail
    while (atomic_load_explicit(&rb->prod.tail, memory_order_relaxed) != prod_head)
        kt_pause();

    atomic_store_explicit(&rb->prod.tail, prod_next, memory_order_release);

end:
    if (free_space != NULL)
        *free_space = free_entries - n;

    return n;
}

u32 kt_ringbuf_enqueue_burst(struct kt_ringbuf *rb, const void *obj_table, u32 esize, u32 n, u32 *free_space)
{
    return __kt_ringbuf_do_enqueue_elems(rb, obj_table, esize, n, KT_RB_BEHAVIOR_VARIABLE, free_space);
}

static inline u32
__kt_ringbuf_move_cons_head(struct kt_ringbuf *rb, u32 n, enum kt_ringbuf_behavior behavior, u32 *old_head,
                            u32 *new_head, u32 *entries)
{
    u32 prod_tail;
    u32 max = n;

    int success;

    *old_head = atomic_load_explicit(&rb->cons.head, memory_order_relaxed);
    do
    {
        n = max;

        atomic_thread_fence(memory_order_acquire);

        prod_tail = atomic_load_explicit(&rb->prod.tail, memory_order_acquire);

        *entries = prod_tail - *old_head;

        if (unlikely(n > *entries))
            n = (behavior == KT_RB_BEHAVIOR_FIXED) ? 0 : *entries;

        if (unlikely(n == 0))
            return 0;

        *new_head = *old_head + n;

        success = atomic_compare_exchange_weak_explicit(&rb->cons.head, old_head, *new_head,
                                                        memory_order_relaxed, memory_order_relaxed);
    } while (unlikely(success == 0));

    return n;
}

static inline void
__kt_ringbuf_dequeue_elems(struct kt_ringbuf *rb, u32 cons_head, void *obj_table, u32 esize, u32 n)
{
    if (esize == 8)
    {
        u32 i;
        const u32 size = rb->size;
        u32 idx = cons_head & (size - 1);
        u64 *ring = (u64 *)&rb[1];
        u64 *dst = (u64 *)obj_table;

        if (likely(idx + n <= size))
        {
            for (i = 0; i < (n & ~0x3); i += 4, idx += 4)
            {
                dst[i] = ring[idx];
                dst[i + 1] = ring[idx + 1];
                dst[i + 2] = ring[idx + 2];
                dst[i + 3] = ring[idx + 3];
            }
            switch (n & 0x3)
            {
            case 3:
                dst[i++] = ring[idx++]; // fallthrough
            case 2:
                dst[i++] = ring[idx++]; // fallthrough
            case 1:
                dst[i++] = ring[idx++]; // fallthrough
            }
        }
        else
        {
            for (i = 0; i < n; i++, idx++)
            {
                dst[i] = ring[idx];
            }
            for (idx = 0; i < n; i++, idx++)
            {
                dst[i] = ring[idx];
            }
        }
    }
    else
    {
        assert("not implemented" == 0);
    }
}

static inline u32
__kt_ringbuf_do_dequeue_elems(struct kt_ringbuf *rb, void *obj_table, u32 esize, u32 n,
                              enum kt_ringbuf_behavior behavior, u32 *available)
{
    u32 cons_head, cons_next;
    u32 entries;

    n = __kt_ringbuf_move_cons_head(rb, n, behavior, &cons_head, &cons_next, &entries);
    if (n == 0)
        goto end;

    // now we can dequeue entries from the ring
    __kt_ringbuf_dequeue_elems(rb, cons_head, obj_table, esize, n);

    // NOTE(garbu): update consumer tail
    while (atomic_load_explicit(&rb->cons.tail, memory_order_relaxed) != cons_head)
        kt_pause();

    atomic_store_explicit(&rb->cons.tail, cons_next, memory_order_release);

end:
    if (available != NULL)
        *available = entries - n;

    return n;
}

u32 kt_ringbuf_dequeue_burst(struct kt_ringbuf *rb, void *obj_table, u32 esize, u32 n, u32 *available)
{
    return __kt_ringbuf_do_dequeue_elems(rb, obj_table, esize, n, KT_RB_BEHAVIOR_VARIABLE, available);
}