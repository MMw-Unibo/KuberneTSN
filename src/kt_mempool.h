#ifndef KT_MEMPOOL_H
#define KT_MEMPOOL_H

#include "kt_common.h"
#include "kt_alloc.h"
#include "kt_ringbuf.h"

struct kt_mempool
{
#define KT_MEMPOOL_NAME_SIZE 64
    char name[KT_MEMPOOL_NAME_SIZE];
    struct kt_ringbuf *buf;

    struct kt_alloc *alloc;

    i32 esize;
    i32 count;
    i32 size;
    i32 capacity;
};

struct kt_mempool *kt_mempool_create(struct kt_allocator *al, char *name, i32 esize, i32 count);

struct kt_mempool *kt_mempool_lookup(const char *name);

#endif // KT_MEMPOOL_H