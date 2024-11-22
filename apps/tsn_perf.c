#include <getopt.h>
#include <signal.h>

#include <arpa/inet.h>

#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/net_tstamp.h>
#include <linux/sockios.h>

#include <net/if.h>

#include <sys/socket.h>
#include <sys/ioctl.h>

#include <kt_common.h>

#define exit_with_error(s)                 \
    {                                      \
        fprintf(stderr, "Error: %s\n", s); \
        exit(EXIT_FAILURE);                \
    }

/* Structs */

/**
 * @brief Application configuration
 *
 * @param is_talker 1 if the application is a talker, 0 if it is a listener
 * @param port UDP port to use
 * @param addr IP address to use. if is_talker is 1, this is the listener address, otherwise it the address to bind to
 * @param n_msgs Number of messages to send
 * @param msg_size Size of each message
 * @param use_txtime 1 if the application should use SO_TXTIME, 0 otherwise
 * @param wakeup_delay Time to wait before sending the first message
 * @param interval Time between messages
 * @param priority Priority of the socket
 * @param verbose 1 if the application should print verbose messages, 0 otherwise
 */
struct app_config
{
    int is_talker;
    int port;
    char addr[64];
    int n_msgs;
    int msg_size;
    int use_txtime;
    int64_t wakeup_delay;
    int64_t interval;
    int64_t priority;
    int verbose;
};

/* Globals */

static int g_run = 1;

#define DEFAULT_PRIORITY 0
#define DEFAULT_MSG_SIZE 512
#define DEFAULT_N_MSGS 1000
#define DEFAULT_PORT 9999
#define DEFAULT_ADDR "192.168.100.12"
#define DEFAULT_INTERVAL 1000000LL    // 1ms
#define DEFAULT_WAKEUP_DELAY 100000LL // 100us

struct app_config default_config = {
    .is_talker = 1,
    .port = DEFAULT_PORT,
    .n_msgs = DEFAULT_N_MSGS,
    .msg_size = DEFAULT_MSG_SIZE,
    .use_txtime = 0,
    .priority = DEFAULT_PRIORITY,
    .interval = DEFAULT_INTERVAL,
    .wakeup_delay = DEFAULT_WAKEUP_DELAY,
    .verbose = 0,
};

void sig_handler(int signum)
{
    (void)signum;
    g_run = 0;
}

/* Functions */
int init_tx_socket(int priority)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        fprintf(stderr, "socket failed: %s\n", strerror(errno));
        return -1;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority)) < 0)
        exit_with_error("setsockopt SO_PRIORITY");

    int timestamping_flags = SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE;
    if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, &timestamping_flags,
                   sizeof(timestamping_flags)) < 0)
        exit_with_error("setsockopt SO_TIMESTAMPING");

    static struct sock_txtime sk_txtime = {
        .clockid = CLOCK_TAI,
        .flags = 0, // SOF_TXTIME_DEADLINE_MODE | SOF_TXTIME_REPORT_ERRORS,
    };

    if (setsockopt(sock, SOL_SOCKET, SO_TXTIME, &sk_txtime, sizeof(sk_txtime)))
    {
        fprintf(stderr, "WARN: SO_TXTIME not supported\n");
    }

    return sock;
}

int init_rx_socket(struct app_config *config)
{
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        fprintf(stderr, "socket failed: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_in sk_addr = {0};
    sk_addr.sin_family = AF_INET;
    sk_addr.sin_port = htons(config->port);
    sk_addr.sin_addr.s_addr = inet_addr(config->addr);

    if (bind(sockfd, (struct sockaddr *)&sk_addr, (socklen_t)sizeof(sk_addr)) < 0)
    {
        fprintf(stderr, "bind failed: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    return sockfd;
}

void do_talker(struct app_config *config)
{
    int sockfd = init_tx_socket(config->priority);
    if (sockfd < 0)
    {
        fprintf(stderr, "cannot init TX socket\n");
        return;
    }

    struct sockaddr_in sk_addr = {0};
    sk_addr.sin_port = htons(config->port);
    sk_addr.sin_family = AF_INET;
    sk_addr.sin_addr.s_addr = inet_addr(config->addr);

    struct cmsghdr *cmsg;
    struct msghdr msg;
    struct iovec iov;
    char control[CMSG_SPACE(sizeof(uint64_t))] = {0};

    /* Construct the packet msghdr, CMSG and initialize packet payload */
    int msg_size = config->msg_size;
    char *message = (char *)malloc(msg_size);
    memset(message, 'a', msg_size);

    int32_t *msg_cnt = (int32_t *)message;

    iov.iov_base = message;
    iov.iov_len = (size_t)msg_size;

    memset(&msg, 0, sizeof(msg));
    msg.msg_name = (void *)&sk_addr;
    msg.msg_namelen = sizeof(struct sockaddr_in);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (config->use_txtime)
    {
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);

        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_TXTIME;
        cmsg->cmsg_len = CMSG_LEN(sizeof(uint64_t));
    }

    int ret;
    int nb_msgs = config->n_msgs;

    int64_t now = kt_get_realtime_ns();
    int64_t now_norm = (now / NSEC_PER_SEC) * NSEC_PER_SEC;

    int64_t txtime = (now_norm + (NSEC_PER_SEC * 2));
    int64_t wakeup_time = txtime - config->wakeup_delay;

    fprintf(stderr, "now: %ld, now_norm: %ld, txtime: %ld, wakeup_time: %ld\n",
            now, now_norm, txtime, wakeup_time);

    struct timespec sleep_ts =
        {
            .tv_sec = (wakeup_time / NSEC_PER_SEC),
            .tv_nsec = (wakeup_time % NSEC_PER_SEC)};

    int counter = 1;
    fprintf(stderr, "Starting talker\n");
    while (g_run && counter < nb_msgs)
    {
        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &sleep_ts, NULL);

        /* Update CMSG tx_timestamp and payload before sending */
        if (config->use_txtime)
            *((uint64_t *)CMSG_DATA(cmsg)) = txtime;

        msg_cnt[0] = counter;

        int64_t send_time = kt_get_realtime_ns();
        ret = sendmsg(sockfd, &msg, 0);
        if (ret < 1)
        {
            // printf("sendmsg failed: %s\n", strerror(errno));
        }
        else
        {
            if (config->verbose)
                printf("%d, %ld, %ld, %ld\n", counter, wakeup_time, txtime, send_time);

            counter++;
        }

        txtime += config->interval;
        wakeup_time += config->interval;
        sleep_ts.tv_sec = (wakeup_time / NSEC_PER_SEC);
        sleep_ts.tv_nsec = (wakeup_time % NSEC_PER_SEC);
    }
}

void do_listener(struct app_config *config)
{
    signal(SIGINT, sig_handler);

    int sockfd = init_rx_socket(config);

    char msg[4096];
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int32_t counter = 0;
    while (g_run)
    {
        int ret = recvfrom(sockfd, msg, sizeof(msg), MSG_DONTWAIT, (struct sockaddr *)&addr, &addrlen);
        if (ret < 0 && errno != EAGAIN)
        {
            // NOTE(garbu): this is not an error, just a timeout
        }
        else if (ret > 0)
        {
            int64_t now = kt_get_realtime_ns();

            counter = *(int32_t *)msg;

            if (config->verbose)
                printf("%d, %ld\n", counter++, now);
        }
    }
}

int main(int argc, char *argv[])
{
    //  do getopt
    struct app_config config = default_config;
    strncpy(config.addr, DEFAULT_ADDR, sizeof(config.addr) - 1);
    int opt;
    while ((opt = getopt(argc, argv, "i:w:p:n:s:a:tv")) != -1)
    {
        switch (opt)
        {
        case 'p':
            config.port = atoi(optarg);
            break;
        case 'n':
            config.n_msgs = atoi(optarg);
            break;
        case 's':
            config.msg_size = atoi(optarg);
            break;
        case 'a':
            strncpy(config.addr, optarg, sizeof(config.addr) - 1);
            break;
        case 'i':
            config.interval = atol(optarg);
            break;
        case 'w':
            config.wakeup_delay = atol(optarg);
            break;
        case 't':
            config.use_txtime = 1;
            break;
        case 'v':
            config.verbose = 1;
            break;
        default:
            fprintf(stderr, "Usage: %s [-p port] [-n n_msgs] [-s msg_size] [-a addr] [-i interval] [-w wakeup_delay] [-t]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (config.port == 0)
    {
        fprintf(stderr, "port must be specified\n");
        exit(EXIT_FAILURE);
    }

    if (config.msg_size <= 0 || config.msg_size > 1450)
    {
        fprintf(stderr, "msg_size must be between 1 and 1450\n");
        exit(EXIT_FAILURE);
    }

    if (config.n_msgs <= 0)
    {
        config.n_msgs = 1;
    }

    char *app_role = getenv("APP_ROLE");
    if (strcmp(app_role, "talker") == 0)
    {
        config.is_talker = 1;
    }
    else if (strcmp(app_role, "listener") == 0)
    {
        config.is_talker = 0;
    }

    if (config.is_talker)
    {
        do_talker(&config);
    }
    else
    {
        do_listener(&config);
    }

    return 0;
}
