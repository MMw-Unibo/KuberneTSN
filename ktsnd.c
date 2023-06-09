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

#include <rte_log.h>
#include <rte_errno.h>
#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip_frag.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

#define IPV4 4
#define IP_UDP 17
#define IP_HEADER_LEN 20

#define SRC_PORT 9999

static int g_run = 1;

void handler(int signum)
{
    (void)signum;
    g_run = 0;
}

// From a string representation of an IPv4 address, give back
// the address as a 32-bit integer in HOST format
static inline int ip_parse(char *addr, uint32_t *dst)
{
    if (inet_pton(AF_INET, addr, dst) != 1)
    {
        return -1;
    }
    *dst = rte_be_to_cpu_32(*dst);
    return 0;
}

// Initializes a device with a single queue
static inline int port_init(uint16_t port_id, struct rte_mempool *mempool, uint16_t mtu)
{
    int valid_port = rte_eth_dev_is_valid_port(port_id);
    if (!valid_port)
    {
        return -1;
    }

    struct rte_eth_dev_info dev_info;
    int retval = rte_eth_dev_info_get(port_id, &dev_info);
    if (retval != 0)
    {
        fprintf(stderr, "[error] cannot get device (port %u) info: %s\n", port_id, strerror(-retval));
        return retval;
    }

    // Derive the actual MTU we can use based on device capabilities and user request
    uint16_t actual_mtu = RTE_MIN(mtu, dev_info.max_mtu);

    // Configure the device
    struct rte_eth_conf port_conf;
    memset(&port_conf, 0, sizeof(port_conf));
    // port_conf.rxmode.mtu = actual_mtu;
    // port_conf.rxmode.split_hdr_size = 0;
    // port_conf.rxmode.offloads |= (RTE_ETH_RX_OFFLOAD_CHECKSUM | RTE_ETH_RX_OFFLOAD_SCATTER);
    // port_conf.txmode.mq_mode = RTE_ETH_MQ_TX_NONE;
    // port_conf.txmode.offloads |= (RTE_ETH_TX_OFFLOAD_IPV4_CKSUM | RTE_ETH_TX_OFFLOAD_MULTI_SEGS);
    // if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
    //     port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;
    const uint16_t rx_rings = 1, tx_rings = 1;
    retval = rte_eth_dev_configure(port_id, rx_rings, tx_rings, &port_conf);
    if (retval != 0)
        return retval;

    // Set the MTU explicitly
    retval = rte_eth_dev_set_mtu(port_id, actual_mtu);
    if (retval != 0)
    {
        printf("Error setting up the MTU (%d)\n", retval);
        return retval;
    }

    uint16_t nb_rxd = 1024;
    uint16_t nb_txd = 1024;
    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port_id, &nb_rxd, &nb_txd);
    if (retval != 0)
        return retval;

    int socket_id = rte_eth_dev_socket_id(port_id);

    for (uint16_t q = 0; q < rx_rings; q++)
    {
        retval = rte_eth_rx_queue_setup(port_id, q, nb_rxd, socket_id, NULL, mempool);
        if (retval != 0)
            return retval;
    }

    struct rte_eth_txconf txconf = dev_info.default_txconf;
    txconf.offloads = port_conf.txmode.offloads;
    for (uint16_t q = 0; q < tx_rings; q++)
    {
        retval = rte_eth_tx_queue_setup(port_id, q, nb_txd, socket_id, &txconf);
        if (retval != 0)
            return retval;
    }

    retval = rte_eth_dev_start(port_id);
    if (retval != 0)
    {
        return retval;
    }

    return 0;
}

static inline void prepare_packet(struct rte_mbuf *tx_buf, void *payload, size_t payload_size,
                                  uint8_t *src_eth_addr, uint8_t *dst_eth_addr, uint32_t src_ip_addr, uint32_t dst_ip_addr, uint16_t dst_udp_port)
{

    // Get a pointer to the packet content (i.e., what will be actually put on the network)
    char *ptr = rte_pktmbuf_mtod(tx_buf, char *);

    /* Ethernet header */
    struct rte_ether_hdr *ehdr = (struct rte_ether_hdr *)ptr;
    memcpy((unsigned char *)ehdr->src_addr.addr_bytes, src_eth_addr, RTE_ETHER_ADDR_LEN);
    memcpy((unsigned char *)ehdr->dst_addr.addr_bytes, dst_eth_addr, RTE_ETHER_ADDR_LEN);
    ehdr->ether_type = htons(RTE_ETHER_TYPE_IPV4);

    /* IP header.
     * Randomly chosen IP addresses. Again, correct IPs must be used in a real application */
    struct rte_ipv4_hdr *ih = (struct rte_ipv4_hdr *)(ehdr + 1);
    ih->dst_addr = rte_cpu_to_be_32(dst_ip_addr);
    ih->src_addr = rte_cpu_to_be_32(src_ip_addr);
    ih->version = IPV4;
    ih->ihl = 0x05;
    ih->type_of_service = 0;
    ih->total_length = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) + RTE_ETHER_ADDR_LEN + 2 + payload_size);
    ih->fragment_offset = 0x0000;
    ih->time_to_live = 64;
    ih->next_proto_id = IP_UDP;
    ih->hdr_checksum = 0x0000;
    ih->packet_id = rte_cpu_to_be_16(ih->packet_id);

    // Checksum
    ih->hdr_checksum = rte_ipv4_cksum(ih);

    /* UDP */
    struct rte_udp_hdr *uh = (struct rte_udp_hdr *)(ih + 1);
    uh->dst_port = rte_cpu_to_be_16(dst_udp_port);
    uh->src_port = rte_cpu_to_be_16(SRC_PORT);
    uh->dgram_len = rte_cpu_to_be_16(sizeof(struct rte_udp_hdr) + payload_size);
    uh->dgram_cksum = 0;

    /* Copy payload content. This copy could be avoided with the DPDK external memory API.
     * In this prototype we do not use it, as payloads are expected to be small. Contact us
     * in case you need more info about that option, which we used for other works.
     */
    char *body = (char *)(uh + 1);
    memcpy(body, payload, payload_size);

    /* Fill mbuf metadata.
     * ATTENTION: these are really important. Packets won't be sent if the length is not set
     * correctly, as the driver uses this info to tell the NIC what to send. The difference between
     * data and packet length is relevant only in case of fragmentation, as well as the next and
     * nb_segs fields which are used to create chains of mbufs (see documentation).
     */
    tx_buf->data_len = tx_buf->pkt_len =
        RTE_ETHER_HDR_LEN + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) + payload_size;
    tx_buf->next = NULL;
    tx_buf->nb_segs = 1;
}

int main(int argc, char *argv[])
{
    printf("ktsnd v0.1\n");

    /********** DPDK-SPECIFIC ARGUMENTS *********/
    /* Initialize DPDK */
    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
    {
        rte_exit(EXIT_FAILURE, "error with EAL initialization\n");
    }
    printf("Eal Init OK\n");

    // Update the number of arguments
    argc -= ret;

    /********** TEST_SPECIFIC ARGUMENTS *********/
    signal(SIGINT, handler);

    // /* Read arguments from cmd and check them */
    // if (argc != 0)
    // {
    //     fprintf(stderr, "Usage: %s <eal_args>\n", argv[ret]);
    //     return -1;
    // }

    /********** TEST-SPECIFIC INITIALIZATION *********/
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

    u32 ring_elem_count = 128;

    struct kt_ringbuf *tx_ring = kt_ringbuf_create(page_al, "RB_tx", ring_elem_count, 1);

    struct kt_ringbuf *free_ring = kt_ringbuf_create(page_al, "RB_free", ring_elem_count, 1);
    for (u32 i = 0; i < kt_ringbuf_get_capacity(free_ring); i++)
    {
        u64 table[1] = {i};
        kt_ringbuf_enqueue_burst(free_ring, table, sizeof(u64), 1, NULL);
    }

    struct kt_mbuf *kt_mbuf_pool = page_al->alloc(page_al, sizeof(struct kt_mbuf) * kt_ringbuf_get_capacity(free_ring));
    struct kt_metadata *kt_metadata_pool = page_al->alloc(page_al, sizeof(struct kt_metadata) * kt_ringbuf_get_capacity(free_ring));

    mem_layout->tx_ring_offset = (u8 *)tx_ring - (u8 *)memory->addr;
    mem_layout->free_ring_offset = (u8 *)free_ring - (u8 *)memory->addr;
    mem_layout->mbuf_pool_offset = (u8 *)kt_mbuf_pool - (u8 *)memory->addr;
    mem_layout->metadata_pool_offset = (u8 *)kt_metadata_pool - (u8 *)memory->addr;

    struct kt_prio_queue prio_queue = kt_prio_queue_init(kt_ringbuf_get_capacity(free_ring));

    /********** DPDK-SPECIFIC INITIALIZATION *********/
    /* Initialize mempool */
    struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create(
        "mbuf_pool", 10240, 64, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (mbuf_pool == NULL)
    {
        LOG_ERROR("Error creating the DPDK mempool: %s\n", rte_strerror(rte_errno));
        return -1;
    }
    LOG_DEBUG("DPDK mempool creation OK\n");

    /* Port init */
    // TODO: Actually, we should iterate on the available devices and get
    // the port_id from it. This is a shortcut that should be avoided. We
    // also configure a single queue (id=0).
    uint16_t port_id = 0;
    uint16_t queue_id = 0;
    ret = port_init(port_id, mbuf_pool, 1500);
    if (ret < 0)
    {
        LOG_ERROR("Error with DPDK port initialization: %s\n", rte_strerror(rte_errno));
    }
    LOG_DEBUG("DPDK port creation OK\n");

    /* Lcore check */
    if (rte_lcore_count() > 1)
    {
        LOG_WARN("DPDK: Too many lcores enabled. Only 1 used.\n");
    }

    // /********** temporary info (to be retrieved from the user) *********/
    // uint8_t src_mac_addr[6];
    // rte_ether_format_addr("00:00:00:00:00:00", 18, src_mac_addr);
    // uint8_t dst_mac_addr[6];
    // rte_ether_format_addr("ff:ff:ff:ff:ff:ff", 18, dst_mac_addr);
    // uint32_t src_ip_addr;
    // ip_parse("10.0.0.1", src_ip_addr);
    // uint32_t dst_ip_addr;
    // ip_parse("10.0.0.2", src_ip_addr);
    // uint16_t dst_udp_port = SRC_PORT;

    /********** DAEMON LOGIC *********/
    i64 tx_delta_ns = 50000LL; // This should be a metadata associated to the p
    i64 counter = 0;
    struct rte_mbuf *tx_buf;
    LOG_INFO("Entering main loop\n");
    while (g_run)
    {
        u64 table[64];
        u32 nb_elem = kt_ringbuf_dequeue_burst(tx_ring, table, sizeof(u64), 8, NULL);
        if (nb_elem > 0)
        {
            for (u32 i = 0; i < nb_elem; i++)
            {
                u64 offset = table[i];
                struct kt_metadata *metadata = (kt_metadata_pool + offset);
                kt_prio_queue_insert(&prio_queue, metadata->txtime, (void *)offset);
            }
        }

        // TX if packets present in the queue
        if (!kt_prio_queue_is_empty(&prio_queue))
        {
            i64 now = kt_get_realtime_ns();
            i64 txtime = kt_prio_queue_getmin(&prio_queue);

            i64 diff = kt_get_time_diff_ns(now, txtime);
            /*
             * CASE 1: diff > tx_delta_ns
             *  - Don't send the packet and wait for the next iteration
             * 
             * ^
             * |    now
             * |     |                       txtime
             * |     |     |<-- tx_delta_ns -->|
             * |_____|_____|___________________|________> time
             * 
             * CASE 2: diff < 0
             *  - Packet lost
             * 
             * ^
             * |                                    now
             * |                            txtime  |
             * |          |<-- tx_delta_ns -->|     |
             * |__________|___________________|_____|___> time
             * 
             * CASE 3: 0 <= diff <= tx_delta_ns
             *  - Send the packet
             * 
             * ^
             * |               now                     
             * |               |              txtime  
             * |          |<-- | tx_delta_ns -->|     
             * |__________|___ |________________|________> time
             */

            if (diff > tx_delta_ns)
            {
                continue;
            }

            if (diff < 0)
            {
                LOG_WARN("DPDK: packet lost\n");
                kt_prio_queue_extract_min(&prio_queue, NULL);
                continue;
            }

            LOG_DEBUG("now=%ld, txtime=%ld, diff=%ld\n", now, txtime, diff);

            u64 mbuf_index;
            kt_prio_queue_extract_min(&prio_queue, &mbuf_index);

            tx_buf = rte_pktmbuf_alloc(mbuf_pool);
            if (!tx_buf)
            {
                LOG_ERROR("DPDK: TX packet buffer allocation failed: %s\n", rte_strerror(rte_errno));
            }

            // TODO: we should take the addresses (MACs, IPs, dst_udp_port) from the pkt metadata.
            // Here, we just assume we know them.
            struct kt_mbuf *mbuf = (kt_mbuf_pool + mbuf_index);
            struct kt_metadata *metadata = (kt_metadata_pool + mbuf_index);

            LOG_DEBUG("DPDK: sending packet of size %d\n", metadata->size);

            /* Fill the first packet with headers and payload */
            prepare_packet(tx_buf, mbuf->data, metadata->size, metadata->eth_src, metadata->eth_dst, metadata->ip_src, metadata->ip_dst, metadata->dport);

            /* Send the packet on the network */
            // i64 send_time = kt_get_realtime_ns();
            u16 nb_tx = rte_eth_tx_burst(port_id, queue_id, &tx_buf, 1);
            (void)nb_tx;
            // LOG_DEBUG("DPDK: sent %u packets\n", nb_tx);

            u64 table[1] = {mbuf_index};
            kt_ringbuf_enqueue_burst(free_ring, &table, sizeof(mbuf_index), 1, NULL);
            // LOG_DEBUG("free mbuf at offset %ld\n", mbuf_index);

            counter++;

#if DEBUG
            i64 end_time = kt_get_realtime_ns();
            f32 end_time_us = (end_time - now) / 1000.0;
            LOG_DEBUG("Packet sent in %.2fus\n", end_time_us);
            i64 end_time_from_txtime = end_time - txtime;
            f32 end_time_from_txtime_us = end_time_from_txtime / 1000.0;
            LOG_DEBUG("Packet sent in %.2fus from txtime\n", end_time_from_txtime_us);
#endif
        }
    }

    LOG_INFO("Exiting main loop\n");

    LOG_DEBUG("Doing cleanup\n");
    kt_memory_destroy(memory_ctrl);
    kt_memory_destroy(memory);
    rte_eal_cleanup();

    return 0;
}
