#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#include <netdb.h>
#include <ifaddrs.h>

#include <arpa/inet.h>

#include <linux/if_packet.h>
// #include <linux/if.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>

#include "kt_memory.h"
#include "kt_logger.h"
#include "kt_ringbuf.h"

static const u8 kt_default_src_mac[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const u8 kt_default_dst_mac[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
// static const u8 kt_default_dst_mac[] = {0xca, 0x15, 0xc5, 0x53, 0x24, 0x72};

struct kt_socket
{
    int fd;     // file descriptor of the socket
    int prio;   // priority of the socket
    int txtime; // flag to indicate if socket is using SO_TXTIME

    LIST_ENTRY(kt_socket)
    list; /* List */
};

struct kt_interface
{
    int ifindex;
    char name[IFNAMSIZ + 1];
    struct sockaddr_in addr;
    struct sockaddr_in netmask;
    u8 mac[6];

    LIST_ENTRY(kt_interface)
    list;
};

void print_interface(struct kt_interface *iface)
{
    printf("Interface: %s\n", iface->name);
    printf("  ifindex: %d\n", iface->ifindex);
    printf("  addr: %s\n", inet_ntoa(iface->addr.sin_addr));
    printf("  netmask: %s\n", inet_ntoa(iface->netmask.sin_addr));
}

LIST_HEAD(kt_socket_list, kt_socket);
LIST_HEAD(kt_interface_list, kt_interface);

static int g_initialized = 0;
static struct kt_memory *g_memory;
static struct kt_memory *g_memory_ctrl;
static struct kt_mem_layout *g_mem_layout;
static struct kt_ringbuf *g_tx_ring;
static struct kt_ringbuf *g_free_ring;
static struct kt_socket_list g_socket_list;
static struct kt_interface_list g_interface_list;
static struct kt_mbuf *g_mbuf_pool;
static struct kt_metadata *g_metadata_pool;

struct kt_socket *kt_socket_find(int fd)
{
    struct kt_socket *sock;
    LIST_FOREACH(sock, &g_socket_list, list)
    {
        if (sock->fd == fd)
            return sock;
    }

    return NULL;
}

struct kt_interface *kt_interface_find(int ifindex)
{
    struct kt_interface *iface;
    LIST_FOREACH(iface, &g_interface_list, list)
    {
        if (iface->ifindex == ifindex)
            return iface;
    }

    return NULL;
}

int kt_interface_exists(int ifindex)
{
    struct kt_interface *iface;
    LIST_FOREACH(iface, &g_interface_list, list)
    {
        if (iface->ifindex == ifindex)
            return 1;
    }

    return 0;
}

int is_same_subnetwork(struct sockaddr_in *addr1, struct sockaddr_in *addr2, struct sockaddr_in *subnet_mask)
{
    uint32_t network1 = addr1->sin_addr.s_addr & subnet_mask->sin_addr.s_addr;
    uint32_t network2 = addr2->sin_addr.s_addr & subnet_mask->sin_addr.s_addr;

    return network1 == network2;
}

struct kt_interface *kt_interface_get_by_net(struct sockaddr_in *addr)
{
    if (addr == NULL)
    {
        LOG_TRACE("addr is NULL");
        return NULL;
    }

    struct kt_interface *interface;
    LIST_FOREACH(interface, &g_interface_list, list)
    {
        if (is_same_subnetwork(&interface->addr, addr, &interface->netmask))
            return interface;
    }

    return NULL;
}

static int (*__start_main)(int (*main)(int, char **, char **), int argc,
                           char **ubp_av, void (*init)(void),
                           void (*fini)(void), void (*rtld_fini)(void),
                           void(*stack_end));

static int (*default_fcntl)(int fildes, int cmd, ...) = NULL;
static int (*default_setsockopt)(int fd, int level, int optname,
                                 const void *optval, socklen_t optlen) = NULL;
static int (*default_getsockopt)(int fd, int level, int optname,
                                 const void *optval, socklen_t *optlen) = NULL;
static int (*default_read)(int sockfd, void *buf, size_t len) = NULL;
static int (*default_write)(int sockfd, const void *buf, size_t len) = NULL;
static int (*default_connect)(int sockfd, const struct sockaddr *addr,
                              socklen_t addrlen) = NULL;
static int (*default_socket)(int domain, int type, int protocol) = NULL;
static int (*default_close)(int fildes) = NULL;
static int (*default_bind)(int sockfd, const struct sockaddr *addr,
                           socklen_t addrlen) = NULL;
static int (*default_poll)(struct pollfd fds[], nfds_t nfds,
                           int timeout) = NULL;
static int (*default_pollchk)(struct pollfd *__fds, nfds_t __nfds,
                              int __timeout, __SIZE_TYPE__ __fdslen) = NULL;

static int (*default_ppoll)(struct pollfd *fds, nfds_t nfds,
                            const struct timespec *tmo_p,
                            const sigset_t *sigmask) = NULL;
static int (*default_select)(int nfds, fd_set *restrict readfds,
                             fd_set *restrict writefds,
                             fd_set *restrict errorfds,
                             struct timeval *restrict timeout);
static ssize_t (*default_send)(int sockfd, const void *message, size_t length,
                               int flags) = NULL;
static ssize_t (*default_sendto)(int sockfd, const void *message, size_t length,
                                 int flags, const struct sockaddr *dest_addr,
                                 socklen_t dest_len) = NULL;
static ssize_t (*default_sendmsg)(int sockfd, const struct msghdr *msg, int flags) = NULL;
static ssize_t (*default_recvfrom)(int sockfd, void *buf, size_t len,
                                   int flags, struct sockaddr *restrict address,
                                   socklen_t *restrict addrlen) = NULL;
static int (*default_getpeername)(int socket, struct sockaddr *restrict address,
                                  socklen_t *restrict address_len) = NULL;
static int (*default_getsockname)(int socket, struct sockaddr *restrict address,
                                  socklen_t *restrict address_len) = NULL;

int socket(int domain, int type, int protocol)
{
    int fd = default_socket(domain, type, protocol);
    if (fd < 0)
        return fd;

    // check if socket exists
    struct kt_socket *node = kt_socket_find(fd);
    if (!node)
    {
        node = (struct kt_socket *)malloc(sizeof(struct kt_socket));
        node->fd = fd;
        node->prio = -1;
        node->txtime = 0;

        LIST_INSERT_HEAD(&g_socket_list, node, list);
    }
}

int setsockopt(int fd, int level, int optname,
               const void *optval, socklen_t optlen)
{
    if (level == SOL_SOCKET)
    {
        switch (optname)
        {
        case SO_TXTIME:
        {
            if (optlen != sizeof(uint64_t))
            {
                errno = EINVAL;
                return -1;
            }

            struct kt_socket *node = kt_socket_find(fd);
            if (!node)
            {
                node = (struct kt_socket *)malloc(sizeof(struct kt_socket));
                node->fd = fd;
                node->prio = -1;
                node->txtime = 1;

                LIST_INSERT_HEAD(&g_socket_list, node, list);
            }
            else
            {
                node->txtime = 1;
            }

            return 0;
        }
        case SO_PRIORITY:
        {
            if (optlen != sizeof(int))
            {
                errno = EINVAL;
                return -1;
            }

            struct kt_socket *node = kt_socket_find(fd);
            if (!node)
            {
                node = (struct kt_socket *)malloc(sizeof(struct kt_socket));
                node->fd = fd;
                node->prio = *(int *)optval;
                node->txtime = 0;

                LIST_INSERT_HEAD(&g_socket_list, node, list);
            }
            else
            {
                node->prio = *(int *)optval;
            }

            return 0;
        }
        }
    }

    return default_setsockopt(fd, level, optname, optval, optlen);
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    // get the socket
    struct kt_socket *node = kt_socket_find(sockfd);
    if (!node)
    {
        LOG_TRACE("sendmsg: socket not found\n");
        return default_sendmsg(sockfd, msg, flags);
    }

    // check if txtime is enabled
    if (!node->txtime)
    {
        LOG_TRACE("sendmsg: txtime not enabled\n");
        return default_sendmsg(sockfd, msg, flags);
    }

    // check if we have a timestamp
    struct cmsghdr *cmsg = NULL;
    bool has_txtime = false;
    u64 txtime = 0;
    for (cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL; cmsg = CMSG_NXTHDR((struct msghdr *)msg, cmsg))
    {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_TXTIME)
        {
            has_txtime = true;
            txtime = *((u64 *)CMSG_DATA(cmsg));
            break;
        }
    }

    if (!has_txtime)
    {
        LOG_TRACE("sendmsg: txtime not found\n");
        return default_sendmsg(sockfd, msg, flags);
    }

    struct sockaddr_in *addr = (struct sockaddr_in *)msg->msg_name;
    struct kt_interface *interface = kt_interface_get_by_net(addr);
    if (!interface)
    {
        LOG_TRACE("sendmsg: interface not found\n");
        return default_sendmsg(sockfd, msg, flags);
    }

    u64 mbuf_index[1];
    u32 nb_free = kt_ringbuf_dequeue_burst(g_free_ring, &mbuf_index, sizeof(u64), 1, NULL);
    if (nb_free == 0)
    {
        LOG_TRACE("sendmsg: no free slots\n");
        return -ENOBUFS;
    }

    // TODO(garbu): handle this in a better way?
    u64 index = mbuf_index[0];

    LOG_TRACE("sendmsg: using index %lu\n", index);

    size_t size = msg->msg_iov[0].iov_len;
    struct kt_mbuf *mbuf = (g_mbuf_pool + index);
    memcpy(mbuf->data, msg->msg_iov[0].iov_base, size);

    struct kt_metadata *metadata = (g_metadata_pool + index);
    metadata->txtime = txtime;
    metadata->size = size;
    memcpy(metadata->eth_src, interface->mac, 6);
    memcpy(metadata->eth_dst, kt_default_dst_mac, 6);
    metadata->ip_dst = ntohl(addr->sin_addr.s_addr);
    metadata->ip_src = ntohl(interface->addr.sin_addr.s_addr);
    metadata->dport = ntohs(addr->sin_port);

    // enqueue the packet
    u32 nb_enqueued = kt_ringbuf_enqueue_burst(g_tx_ring, &mbuf_index, sizeof(u64), 1, NULL);
    if (nb_enqueued != 1)
    {
        LOG_TRACE("sendmsg: failed to enqueue packet\n");
        return -ENOBUFS;
    }

    return size;
}

static int free_socket(int fd)
{
    return default_close(fd);
}

int close(int fildes)
{
    struct kt_socket *node = kt_socket_find(fildes);
    if (node)
    {
        // remove from list
        LIST_REMOVE(node, list);
        free(node);
    }

    return free_socket(fildes);
}

int query_and_add_mac_address(struct kt_interface *iface)
{
    int sock;
    struct ifreq ifr;

    // Open a socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Copy the interface name into ifreq structure
    strncpy(ifr.ifr_name, iface->name, IFNAMSIZ);

    // Get the MAC address
    if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0)
    {
        perror("ioctl");
        exit(EXIT_FAILURE);
    }

    memcpy(iface->mac, ifr.ifr_hwaddr.sa_data, 6);

    close(sock);
}

void query_interfaces()
{
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    // Traverse through the linked list of interfaces
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;

        family = ifa->ifa_addr->sa_family;

        // if the interface is an ipv4 interface add it to the list of interfaces
        if (family == AF_INET)
        {
            s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
            if (s != 0)
            {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                exit(EXIT_FAILURE);
            }

            int ifindex = if_nametoindex(ifa->ifa_name);
            if (kt_interface_exists(ifindex))
            {
                LOG_DEBUG("interface %s already exists\n", ifa->ifa_name);
                continue;
            }

            struct kt_interface *interface = malloc(sizeof(struct kt_interface));
            interface->ifindex = ifindex;
            memcpy(&interface->addr, ifa->ifa_addr, sizeof(struct sockaddr_in));
            memcpy(&interface->netmask, ifa->ifa_netmask, sizeof(struct sockaddr_in));
            strncpy(interface->name, ifa->ifa_name, IFNAMSIZ);
            LIST_INSERT_HEAD(&g_interface_list, interface, list);
        }
    }

    freeifaddrs(ifaddr);

    // query the mac address for each interface
    struct kt_interface *interface;
    LIST_FOREACH(interface, &g_interface_list, list)
    {
        query_and_add_mac_address(interface);
    }

    // print the list of interfaces with: name, index, ip address, netmask,
    LIST_FOREACH(interface, &g_interface_list, list)
    {
        // print_interface(interface);
    }
}

int __libc_start_main(int (*main)(int, char **, char **), int argc,
                      char **ubp_av, void (*init)(void), void (*fini)(void),
                      void (*rtld_fini)(void), void(*stack_end))
{
    __start_main = dlsym(RTLD_NEXT, "__libc_start_main");

    default_send = dlsym(RTLD_NEXT, "send");
    default_sendto = dlsym(RTLD_NEXT, "sendto");
    default_sendmsg = dlsym(RTLD_NEXT, "sendmsg");
    default_recvfrom = dlsym(RTLD_NEXT, "recvfrom");
    default_bind = dlsym(RTLD_NEXT, "bind");
    default_poll = dlsym(RTLD_NEXT, "poll");
    default_ppoll = dlsym(RTLD_NEXT, "ppoll");
    default_pollchk = dlsym(RTLD_NEXT, "__poll_chk");
    default_select = dlsym(RTLD_NEXT, "select");
    default_fcntl = dlsym(RTLD_NEXT, "fcntl");
    default_setsockopt = dlsym(RTLD_NEXT, "setsockopt");
    default_getsockopt = dlsym(RTLD_NEXT, "getsockopt");
    default_read = dlsym(RTLD_NEXT, "read");
    default_write = dlsym(RTLD_NEXT, "write");
    default_connect = dlsym(RTLD_NEXT, "connect");
    default_socket = dlsym(RTLD_NEXT, "socket");
    default_close = dlsym(RTLD_NEXT, "close");
    default_getpeername = dlsym(RTLD_NEXT, "getpeername");
    default_getsockname = dlsym(RTLD_NEXT, "getsockname");

    g_memory = kt_memory_attach(KT_DEFAULT_SHARED_DATA_MEMORY_NAME, KT_DEFAULT_MEMORY_SIZE);
    if (!g_memory)
    {
        LOG_DEBUG("Failed to attach to shared memory '%s'.\n", KT_DEFAULT_SHARED_DATA_MEMORY_NAME);
        return -1;
    }

    g_memory_ctrl = kt_memory_attach(KT_DEFAULT_SHARED_CTRL_MEMORY_NAME, KT_DEFAULT_MEMORY_SIZE);
    if (!g_memory_ctrl)
    {
        LOG_DEBUG("Failed to attach to shared memory '%s'.\n", KT_DEFAULT_SHARED_CTRL_MEMORY_NAME);
        return -1;
    }

    g_mem_layout = (struct kt_mem_layout *)g_memory_ctrl->addr;

    g_tx_ring = (struct kt_ringbuf *)((u8 *)g_memory->addr + g_mem_layout->tx_ring_offset);
    g_free_ring = (struct kt_ringbuf *)((u8 *)g_memory->addr + g_mem_layout->free_ring_offset);
    g_mbuf_pool = (struct kt_mbuf *)((u8 *)g_memory->addr + g_mem_layout->mbuf_pool_offset);
    g_metadata_pool = (struct kt_metadata *)((u8 *)g_memory->addr + g_mem_layout->metadata_pool_offset);

    LIST_INIT(&g_socket_list);
    LIST_INIT(&g_interface_list);

    query_interfaces();

    return __start_main(main, argc, ubp_av, init, fini, rtld_fini, stack_end);
}
