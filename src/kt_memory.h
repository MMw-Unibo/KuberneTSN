#ifndef KT_MEMORY_H
#define KT_MEMORY_H

#include <pthread.h>

#include "kt_common.h"

#define KT_DEFAULT_MEMORY_SIZE (1024 * 1024)
#define KT_DEFAULT_SHARED_DATA_MEMORY_NAME "ktsnd_data_memory"
#define KT_DEFAULT_SHARED_CTRL_MEMORY_NAME "ktsnd_meta_memory"

/**
 * @brief Shared Memory descriptor
 */
struct kt_memory
{
#define KT_MEMORY_NAMESIZE 64
    char name[KT_MEMORY_NAMESIZE];
    i32 fd;

    union
    {
        void *addr;
        u64 addr_64;
    };
    u32 size;
    u32 used;
    u32 flags;
};

struct kt_memory *kt_memory_attach(const char *name, size_t size);

struct kt_memory *kt_memory_create(const char *name, size_t size);

i32 kt_memory_destroy(struct kt_memory *m);

i32 kt_memory_detach(struct kt_memory *m, int (*_close)(int));

struct kt_mbuf {
    u8 data[2048];
};

#define KT_METADATA_TRANSPORT_ETHERNET 0x0001
#define KT_METADATA_TRANSPORT_UDP 0x0002

struct kt_metadata {
    u16 transport;
    u64 txtime;
    u8 eth_src[6];
    u8 eth_dst[6];
    u32 ip_src;
    u32 ip_dst;
    u16 udp_dport;
    size_t size;
};

struct kt_mem_layout
{
    size_t tx_ring_offset;
    size_t free_ring_offset;
    size_t mbuf_pool_offset;
    size_t metadata_pool_offset;
};

#endif // KT_MEMORY_H