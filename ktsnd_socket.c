#include <getopt.h>

#include <arpa/inet.h>

#include <netinet/in.h>

#include <sys/socket.h>

#include <kt_common.h>
#include <kt_logger.h>
#include <kt_memory.h>
#include <kt_queue.h>
#include <kt_alloc.h>
#include <kt_mempool.h>
#include <kt_ringbuf.h>

struct config
{
    char *saddr;
    char *daddr;
    int dport;
};

static int g_run = 1;

void handler(int signum)
{
    (void)signum;
    g_run = 0;
}

int main(int argc, char *argv[])
{
    printf("KTSNd v0.1\n");

    signal(SIGINT, handler);

    struct kt_memory *memory = kt_memory_create(KT_DEFAULT_SHARED_DATA_MEMORY_NAME, KT_DEFAULT_MEMORY_SIZE);
    if (!memory)
    {
        LOG_ERROR("cannot crate shared memory\n");
        return -1;
    }

    size_t page_size = getpagesize();

    struct kt_memory *memory_ctrl = kt_memory_create(KT_DEFAULT_SHARED_CTRL_MEMORY_NAME, page_size);
    if (!memory_ctrl)
    {
        LOG_ERROR("cannot crate shared memory\n");
        return -1;
    }

    struct kt_mem_layout *mem_layout = (struct kt_mem_layout *)memory_ctrl->addr;

    struct kt_allocator *page_al = kt_page_allocator_make(memory->addr, memory->size, page_size);

    size_t ring_elem_count = 100;

    struct kt_ringbuf *tx_ring = kt_ringbuf_create(page_al, "RB_tx", ring_elem_count, 1);

    struct kt_ringbuf *free_ring = kt_ringbuf_create(page_al, "RB_free", ring_elem_count, 1);
    for (size_t i = 0; i < ring_elem_count; i++)
    {
        kt_ringbuf_enqueue_burst(free_ring, &i, sizeof(i), 1, NULL);
    }

    struct kt_mbuf *mbuf_pool = page_al->alloc(page_al, sizeof(struct kt_mbuf) * ring_elem_count);
    struct kt_metadata *metadata_pool = page_al->alloc(page_al, sizeof(struct kt_metadata) * ring_elem_count);

    mem_layout->tx_ring_offset = (u8 *)tx_ring - (u8 *)memory->addr;
    mem_layout->free_ring_offset = (u8 *)free_ring - (u8 *)memory->addr;
    mem_layout->mbuf_pool_offset = (u8 *)mbuf_pool - (u8 *)memory->addr;
    mem_layout->metadata_pool_offset = (u8 *)metadata_pool - (u8 *)memory->addr;

    struct kt_prio_queue prio_queue = kt_prio_queue_init(ring_elem_count);

    i64 wakeup_ns = 10000;
    i64 counter = 0;
    i64 recv_time = 0;
    while (g_run)
    {
        u64 table[64];
        u32 nb_elem = kt_ringbuf_dequeue_burst(tx_ring, table, sizeof(u64), 8, NULL);
        if (nb_elem == 0)
        {
            // LOG_DEBUG("no data\n");
            continue;
        }

        for (u32 i = 0; i < nb_elem; i++)
        {
            u64 offset = table[i];
            struct kt_mbuf *mbuf = (mbuf_pool + offset);
            struct kt_metadata *metadata = (metadata_pool + offset);

            // print metadata
            // u64 txtime;
            // u8 eth_src[6];
            // u8 eth_dst[6];
            // u32 ip_src;
            // u32 ip_dst;
            // u16 dport;
            // size_t size;
            printf("metadata: \n");
            printf("  txtime: %ld\n", metadata->txtime);
            printf("  size: %ld\n", metadata->size);
            printf("  eth_src: %02x:%02x:%02x:%02x:%02x:%02x\n", metadata->eth_src[0], metadata->eth_src[1], metadata->eth_src[2], metadata->eth_src[3], metadata->eth_src[4], metadata->eth_src[5]);
            printf("  eth_dst: %02x:%02x:%02x:%02x:%02x:%02x\n", metadata->eth_dst[0], metadata->eth_dst[1], metadata->eth_dst[2], metadata->eth_dst[3], metadata->eth_dst[4], metadata->eth_dst[5]);
            printf("  ip_src: %d.%d.%d.%d\n", (metadata->ip_src >> 24) & 0xFF, (metadata->ip_src >> 16) & 0xFF, (metadata->ip_src >> 8) & 0xFF, (metadata->ip_src) & 0xFF);
            printf("  ip_dst: %d.%d.%d.%d\n", (metadata->ip_dst >> 24) & 0xFF, (metadata->ip_dst >> 16) & 0xFF, (metadata->ip_dst >> 8) & 0xFF, (metadata->ip_dst) & 0xFF);
            printf("  dport: %d\n", metadata->udp_dport);

            // add everything to the prio queue
            kt_prio_queue_insert(&prio_queue, metadata->txtime, offset);
        }

        i64 now = kt_get_realtime_ns();
        if (prio_queue.size > 0)
        {
            i64 txtime = kt_prio_queue_getmin(&prio_queue);
            if (now > txtime - wakeup_ns)
            {
                u64 mbuf_index;
                if (kt_prio_queue_extract_min(&prio_queue, &mbuf_index) < 0)
                {
                    LOG_ERROR("cannot extract min\n");
                    continue;
                }
 
                int64_t send_time = kt_get_realtime_ns();
                // int res = sendto(sockfd, data, size, MSG_DONTWAIT, (struct sockaddr *)&sk_daddr, (socklen_t)sizeof(sk_daddr));
                // if (res < 0 && errno != EAGAIN)
                // {
                //     // printf("sendto filed: %s (%d)\n", strerror(errno), errno);
                // }
                // if (res < 0)
                // {
                //     // printf("send: %s\n", strerror(errno));
                // }
                // else
                // {
                LOG_DEBUG("send mbuf at offset %ld\n", mbuf_index);

                // TODO(garbu): send using DPDK

                LOG_DEBUG("free mbuf at offset %ld\n", mbuf_index);
                u64 table[1] = {mbuf_index};
                kt_ringbuf_enqueue_burst(free_ring, &table, sizeof(mbuf_index), 1, NULL);
            }
        }
    }

    kt_memory_destroy(memory_ctrl);
    kt_memory_destroy(memory);

    return 0;
}
