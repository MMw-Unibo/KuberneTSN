#include "kt_queue.h"
#include "kt_logger.h"

struct kt_prio_queue kt_prio_queue_init(size_t cap)
{
    struct kt_prio_queue tmp;
    tmp.size = 0;
    tmp.cap = cap;
    tmp.elems = (struct kt_pq_node *)malloc(sizeof(struct kt_pq_node) * cap);

    return tmp;
}

int kt_prio_queue_is_empty(struct kt_prio_queue *q)
{
    return q->size == 0;
}

static inline void _kt_prio_queue_swap(struct kt_pq_node *x, struct kt_pq_node *y)
{
    struct kt_pq_node tmp = *x;
    *x = *y;
    *y = tmp;
}

static inline size_t _kt_prio_queue_parent(int i)
{
    // NOTE(garbu): 2 - 1 = 1, 1 / 2 = 0, because of integer division in C
    return ((i - 1) / 2);
}

static inline size_t _kt_prio_queue_left(int i)
{
    return (2 * i + 1);
}

static inline size_t _kt_prio_queue_right(int i)
{
    return (2 * i + 2);
}

inline i64 kt_prio_queue_getmin(struct kt_prio_queue *q)
{
    return q->elems[0].prio;
}

// this is the shift up operation
static inline void _kt_prio_queue_reorder(struct kt_prio_queue *q, int i)
{
    struct kt_pq_node *meta = &q->elems[i];
    struct kt_pq_node *parent = &q->elems[_kt_prio_queue_parent(i)];

    while (i > 0 && meta->prio < parent->prio)
    {
        _kt_prio_queue_swap(meta, parent);
        i = _kt_prio_queue_parent(i);
        meta = &q->elems[i];
        parent = &q->elems[_kt_prio_queue_parent(i)];
    }
}

int kt_prio_queue_insert(struct kt_prio_queue *q, i64 prio, void *data)
{
    if (q->size == q->cap)
    {
        return -1;
    }

    q->elems[q->size].prio = INT64_MAX;
    q->elems[q->size].data = data;
    q->size++;

    kt_prio_queue_decrease_key(q, q->size - 1, prio);

    return 0;
}

void kt_prio_queue_decrease_key(struct kt_prio_queue *q, size_t i, i64 new_val)
{
    if (new_val > q->elems[i].prio)
    {
        LOG_DEBUG("New value is greater than current value");
        return;
    }

    q->elems[i].prio = new_val;
    _kt_prio_queue_reorder(q, i);
}

static inline int _kt_prio_queue_heapify(struct kt_prio_queue *q, size_t i)
{
    size_t min_idx = i;

    size_t left = _kt_prio_queue_left(i);
    size_t right = _kt_prio_queue_right(i);

    if (left < q->size && q->elems[left].prio < q->elems[min_idx].prio)
    {
        min_idx = left;
    }
    else
    {
        min_idx = i;
    }

    if (right < q->size && q->elems[right].prio < q->elems[min_idx].prio)
    {
        min_idx = right;
    }
    else
    {
        min_idx = i;
    }

    if (min_idx != i)
    {
        _kt_prio_queue_swap(&q->elems[i], &q->elems[min_idx]);
        _kt_prio_queue_heapify(q, min_idx);
    }

    return 0;
}

int kt_prio_queue_extract_min(struct kt_prio_queue *q, void *data)
{
    if (q->size <= 0)
    {
        LOG_DEBUG("Heap underflow: %d", q->size);
        return -1;
    }

    struct kt_pq_node min = q->elems[0];
    if (data != NULL)
    {
        *(void **)data = min.data;
    }
    q->elems[0] = q->elems[q->size - 1];

    q->size--;

    _kt_prio_queue_heapify(q, 0);
    return 0;
}

void kt_prio_queue_delete_key(struct kt_prio_queue *q, size_t i)
{
    kt_prio_queue_decrease_key(q, i, INT32_MIN);
    u64 tmp;
    kt_prio_queue_extract_min(q, &tmp);
}
