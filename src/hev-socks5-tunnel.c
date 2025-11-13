/*
 ============================================================================
 Name        : hev-socks5-tunnel.c
 Author      : hev <r@hev.cc> - Enhanced with Multi-threading
 Copyright   : Copyright (c) 2019 - 2025 hev
 Description : Socks5 Tunnel - Multi-threaded Version
 ============================================================================
 */

#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <lwip/tcp.h>
#include <lwip/udp.h>
#include <lwip/nd6.h>
#include <lwip/netif.h>
#include <lwip/ip4_frag.h>
#include <lwip/ip6_frag.h>
#include <lwip/priv/tcp_priv.h>

#include "hev-exec.h"
#include "hev-config.h"
#include "hev-logger.h"
#include "hev-tunnel.h"
#include "hev-compiler.h"
#include "hev-mapped-dns.h"
#include "hev-config-const.h"
#include "hev-thread-pool.h"
#include "hev-tunnel-io.h"
#include "hev-socks5-session-tcp.h"
#include "hev-socks5-session-udp.h"

#include "hev-socks5-tunnel.h"

/* Global state */
static volatile int run = 0;
static int tun_fd = -1;
static int tun_fd_local = 0;
static volatile int session_count = 0;

/* Threading infrastructure */
static HevThreadPool *thread_pool = NULL;
static HevTunnelIO *tunnel_io = NULL;
static pthread_t timer_thread;
static pthread_mutex_t lwip_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Network interface */
static struct netif netif;
static struct tcp_pcb *tcp;
static struct udp_pcb *udp;

/* Session tracking */
typedef struct _SessionNode SessionNode;
struct _SessionNode
{
    void *session;
    SessionNode *next;
    SessionNode *prev;
};

static SessionNode *session_list_head = NULL;
static SessionNode *session_list_tail = NULL;

/* Forward declarations */
static void packet_read_callback (struct pbuf *p, void *user_data);
static void *timer_thread_func (void *arg);

/* ========================================================================
 * Session Management
 * ======================================================================== */

static void
insert_session (void *session)
{
    SessionNode *node;
    int max_sessions;

    node = (SessionNode *)malloc (sizeof (SessionNode));
    if (!node)
        return;

    node->session = session;
    node->next = NULL;

    pthread_mutex_lock (&session_mutex);

    /* Add to tail */
    node->prev = session_list_tail;
    if (session_list_tail)
        session_list_tail->next = node;
    else
        session_list_head = node;
    session_list_tail = node;

    session_count++;

    /* Enforce session limit */
    max_sessions = hev_config_get_misc_max_session_count ();
    if (max_sessions > 0 && session_count > max_sessions) {
        /* Terminate oldest session */
        if (session_list_head && session_list_head->session) {
            LOG_W ("session limit reached, terminating oldest session");
            /* TODO: Add session termination callback */
        }
    }

    pthread_mutex_unlock (&session_mutex);
}

static void
remove_session (void *session)
{
    SessionNode *node;

    pthread_mutex_lock (&session_mutex);

    /* Find and remove node */
    for (node = session_list_head; node; node = node->next) {
        if (node->session == session) {
            if (node->prev)
                node->prev->next = node->next;
            else
                session_list_head = node->next;

            if (node->next)
                node->next->prev = node->prev;
            else
                session_list_tail = node->prev;

            free (node);
            session_count--;
            break;
        }
    }

    pthread_mutex_unlock (&session_mutex);
}

/* ========================================================================
 * Session Task Wrappers
 * ======================================================================== */

typedef struct
{
    void *session;
    void (*run_func) (void *);
} SessionTaskData;

static void
session_task_wrapper (void *data)
{
    SessionTaskData *task_data = (SessionTaskData *)data;

    LOG_D ("session task started");

    /* Run the session */
    task_data->run_func (task_data->session);

    /* Clean up */
    remove_session (task_data->session);
    free (task_data);

    LOG_D ("session task completed");
}

/* ========================================================================
 * LwIP Callbacks
 * ======================================================================== */

static err_t
netif_output_handler (struct netif *netif, struct pbuf *p)
{
    int res;

    res = hev_tunnel_io_write (tunnel_io, p);
    if (res < 0) {
        if (errno == EAGAIN)
            return ERR_WOULDBLOCK;
        LOG_W ("tunnel write error");
        return ERR_IF;
    }

    return ERR_OK;
}

static err_t
netif_output_v4_handler (struct netif *netif, struct pbuf *p,
                         const ip4_addr_t *ipaddr)
{
    return netif_output_handler (netif, p);
}

static err_t
netif_output_v6_handler (struct netif *netif, struct pbuf *p,
                         const ip6_addr_t *ipaddr)
{
    return netif_output_handler (netif, p);
}

static err_t
netif_init_handler (struct netif *netif)
{
    netif->output = netif_output_v4_handler;
    netif->output_ip6 = netif_output_v6_handler;
    return ERR_OK;
}

static err_t
tcp_accept_handler (void *arg, struct tcp_pcb *pcb, err_t err)
{
    SessionTaskData *task_data;
    void *tcp_session;

    if (err != ERR_OK)
        return err;

    if (!run)
        return ERR_RST;

    LOG_D ("accepting new TCP connection");

    /* Create TCP session */
    pthread_mutex_lock (&lwip_mutex);
    tcp_session = hev_socks5_session_tcp_new (pcb, &lwip_mutex);
    pthread_mutex_unlock (&lwip_mutex);

    if (!tcp_session)
        return ERR_MEM;

    /* Create task data */
    task_data = (SessionTaskData *)malloc (sizeof (SessionTaskData));
    if (!task_data) {
        /* TODO: Free session */
        return ERR_MEM;
    }

    task_data->session = tcp_session;
    task_data->run_func = (void (*) (void *))hev_socks5_session_run;

    /* Track session */
    insert_session (tcp_session);

    /* Submit to thread pool */
    if (hev_thread_pool_submit (thread_pool, session_task_wrapper,
                                task_data) < 0) {
        LOG_E ("failed to submit TCP session to thread pool");
        remove_session (tcp_session);
        free (task_data);
        return ERR_MEM;
    }

    return ERR_OK;
}

static void
udp_recv_handler (void *arg, struct udp_pcb *pcb, struct pbuf *p,
                  const ip_addr_t *addr, u16_t port)
{
    SessionTaskData *task_data;
    void *udp_session;
    HevMappedDNS *dns;

    if (!run) {
        pbuf_free (p);
        udp_remove (pcb);
        return;
    }

    /* Check for DNS mapping */
    dns = hev_mapped_dns_get ();
    if (dns && addr->type == IPADDR_TYPE_V4) {
        int faddr = hev_config_get_mapdns_address ();
        int fport = hev_config_get_mapdns_port ();
        if (fport == port && faddr == ip_2_ip4 (addr)->addr) {
            /* Handle DNS query */
            struct pbuf *b;
            int res;

            b = pbuf_alloc (PBUF_TRANSPORT, 512, PBUF_RAM);
            if (b) {
                pthread_mutex_lock (&lwip_mutex);
                res = hev_mapped_dns_handle (dns, p->payload, p->len,
                                            b->payload, b->len);
                if (res > 0) {
                    b->len = res;
                    b->tot_len = res;
                    udp_sendfrom (pcb, b, &pcb->local_ip, pcb->local_port);
                }
                pthread_mutex_unlock (&lwip_mutex);
                pbuf_free (b);
            }
            pbuf_free (p);
            udp_recv (pcb, NULL, NULL);
            udp_remove (pcb);
            return;
        }
    }

    pbuf_free (p);

    LOG_D ("accepting new UDP connection");

    /* Create UDP session */
    pthread_mutex_lock (&lwip_mutex);
    udp_session = hev_socks5_session_udp_new (pcb, &lwip_mutex);
    pthread_mutex_unlock (&lwip_mutex);

    if (!udp_session) {
        udp_remove (pcb);
        return;
    }

    /* Create task data */
    task_data = (SessionTaskData *)malloc (sizeof (SessionTaskData));
    if (!task_data) {
        udp_remove (pcb);
        return;
    }

    task_data->session = udp_session;
    task_data->run_func = (void (*) (void *))hev_socks5_session_run;

    /* Track session */
    insert_session (udp_session);

    /* Submit to thread pool */
    if (hev_thread_pool_submit (thread_pool, session_task_wrapper,
                                task_data) < 0) {
        LOG_E ("failed to submit UDP session to thread pool");
        remove_session (udp_session);
        free (task_data);
        udp_remove (pcb);
        return;
    }
}

/* ========================================================================
 * Packet Processing
 * ======================================================================== */

static void
packet_read_callback (struct pbuf *p, void *user_data)
{
    if (!p)
        return;

    /* Process packet through LwIP */
    pthread_mutex_lock (&lwip_mutex);
    if (netif.input (p, &netif) != ERR_OK) {
        pbuf_free (p);
    }
    pthread_mutex_unlock (&lwip_mutex);
}

/* ========================================================================
 * Timer Thread
 * ======================================================================== */

static void *
timer_thread_func (void *arg)
{
    unsigned int counter = 0;

    LOG_I ("timer thread started");

    while (run) {
        usleep (TCP_TMR_INTERVAL * 1000);

        pthread_mutex_lock (&lwip_mutex);

        tcp_tmr ();

        if ((counter & 3) == 0) {
#if IP_REASSEMBLY
            ip_reass_tmr ();
#endif
#if LWIP_IPV6
            nd6_tmr ();
#if LWIP_IPV6_REASS
            ip6_reass_tmr ();
#endif
#endif
        }

        pthread_mutex_unlock (&lwip_mutex);

        counter++;
    }

    LOG_I ("timer thread stopped");
    return NULL;
}

/* ========================================================================
 * Tunnel Initialization
 * ======================================================================== */

static int
tunnel_init (int extern_tun_fd)
{
    const char *script_path, *name, *ipv4, *ipv6;
    int res;
    unsigned int mtu;

    if (extern_tun_fd >= 0) {
        int nonblock = 1;
        res = ioctl (extern_tun_fd, FIONBIO, (char *)&nonblock);
        if (res < 0) {
            LOG_E ("failed to set tunnel non-blocking");
            return -1;
        }
        tun_fd = extern_tun_fd;
        return 0;
    }

    name = hev_config_get_tunnel_name ();
    tun_fd = hev_tunnel_open (name, 0); /* multi-queue handled internally */
    if (tun_fd < 0) {
        LOG_E ("failed to open tunnel: %s", strerror (errno));
        return -1;
    }

    mtu = hev_config_get_tunnel_mtu ();
    res = hev_tunnel_set_mtu (mtu);
    if (res < 0) {
        LOG_E ("failed to set tunnel MTU");
        return -1;
    }

    ipv4 = hev_config_get_tunnel_ipv4_address ();
    if (ipv4) {
        res = hev_tunnel_set_ipv4 (ipv4, 32);
        if (res < 0) {
            LOG_E ("failed to set tunnel IPv4");
            return -1;
        }
    }

    ipv6 = hev_config_get_tunnel_ipv6_address ();
    if (ipv6) {
        res = hev_tunnel_set_ipv6 (ipv6, 128);
        if (res < 0) {
            LOG_E ("failed to set tunnel IPv6");
            return -1;
        }
    }

    res = hev_tunnel_set_state (1);
    if (res < 0) {
        LOG_E ("failed to bring tunnel up");
        return -1;
    }

    script_path = hev_config_get_tunnel_post_up_script ();
    if (script_path)
        hev_exec_run (script_path, hev_tunnel_get_name (),
                      hev_tunnel_get_index (), 0);

    tun_fd_local = 1;
    return 0;
}

static void
tunnel_fini (void)
{
    const char *script_path;

    if (!tun_fd_local)
        return;

    script_path = hev_config_get_tunnel_pre_down_script ();
    if (script_path)
        hev_exec_run (script_path, hev_tunnel_get_name (),
                      hev_tunnel_get_index (), 1);

    hev_tunnel_close (tun_fd);
    tun_fd_local = 0;
    tun_fd = -1;
}

/* ========================================================================
 * Gateway Initialization
 * ======================================================================== */

static int
gateway_init (void)
{
    ip4_addr_t addr4, mask, gw;
    ip6_addr_t addr6;

    netif_add_noaddr (&netif, NULL, netif_init_handler, ip_input);

    ip4_addr_set_loopback (&addr4);
    ip4_addr_set_any (&mask);
    ip4_addr_set_any (&gw);
    netif_set_addr (&netif, &addr4, &mask, &gw);

    ip6_addr_set_loopback (&addr6);
    netif_add_ip6_address (&netif, &addr6, NULL);

    netif_set_up (&netif);
    netif_set_link_up (&netif);
    netif_set_default (&netif);
    netif_set_flags (&netif, NETIF_FLAG_PRETEND_TCP);

    tcp = tcp_new_ip_type (IPADDR_TYPE_ANY);
    tcp_bind_netif (tcp, &netif);
    tcp_bind (tcp, NULL, 0);
    tcp = tcp_listen (tcp);
    tcp_accept (tcp, tcp_accept_handler);

    udp = udp_new_ip_type (IPADDR_TYPE_ANY);
    udp_bind_netif (udp, &netif);
    udp_bind (udp, NULL, 0);
    udp_recv (udp, udp_recv_handler, NULL);

    LOG_I ("gateway initialized");
    return 0;
}

static void
gateway_fini (void)
{
    if (udp)
        udp_remove (udp);
    if (tcp)
        tcp_close (tcp);
    netif_remove (&netif);
}

/* ========================================================================
 * DNS Mapping
 * ======================================================================== */

static int
mapped_dns_init (void)
{
    HevMappedDNS *dns;
    int cache_size, network, netmask;

    network = hev_config_get_mapdns_network ();
    netmask = hev_config_get_mapdns_netmask ();
    cache_size = hev_config_get_mapdns_cache_size ();

    if (!cache_size)
        return 0;

    dns = hev_mapped_dns_new (network, netmask, cache_size);
    if (!dns)
        return -1;

    hev_mapped_dns_put (dns);
    LOG_I ("mapped DNS initialized");
    return 0;
}

static void
mapped_dns_fini (void)
{
    HevMappedDNS *dns = hev_mapped_dns_get ();
    if (dns) {
        hev_object_unref (HEV_OBJECT (dns));
        hev_mapped_dns_put (NULL);
    }
}

/* ========================================================================
 * Public API
 * ======================================================================== */

int
hev_socks5_tunnel_init (int extern_tun_fd)
{
    int res;
    unsigned int mtu;

    LOG_I ("initializing socks5 tunnel (multi-threaded)");

    signal (SIGPIPE, SIG_IGN);

    /* Initialize tunnel */
    res = tunnel_init (extern_tun_fd);
    if (res < 0)
        goto error;

    /* Initialize LwIP gateway */
    pthread_mutex_lock (&lwip_mutex);
    lwip_init ();
    res = gateway_init ();
    pthread_mutex_unlock (&lwip_mutex);
    if (res < 0)
        goto error;

    /* Initialize DNS mapping */
    res = mapped_dns_init ();
    if (res < 0)
        goto error;

    /* Create thread pool (auto-detect optimal size) */
    thread_pool = hev_thread_pool_new (0);
    if (!thread_pool) {
        LOG_E ("failed to create thread pool");
        goto error;
    }

    /* Create tunnel I/O manager */
    mtu = hev_config_get_tunnel_mtu ();
    tunnel_io = hev_tunnel_io_new (tun_fd, mtu);
    if (!tunnel_io) {
        LOG_E ("failed to create tunnel I/O");
        goto error;
    }

    hev_tunnel_io_set_read_callback (tunnel_io, packet_read_callback, NULL);

    LOG_I ("socks5 tunnel initialized successfully");
    return 0;

error:
    hev_socks5_tunnel_fini ();
    return -1;
}

void
hev_socks5_tunnel_fini (void)
{
    LOG_I ("finalizing socks5 tunnel");

    if (tunnel_io) {
        hev_tunnel_io_destroy (tunnel_io);
        tunnel_io = NULL;
    }

    if (thread_pool) {
        hev_thread_pool_destroy (thread_pool);
        thread_pool = NULL;
    }

    mapped_dns_fini ();
    gateway_fini ();
    tunnel_fini ();

    /* Clear session list */
    pthread_mutex_lock (&session_mutex);
    while (session_list_head) {
        SessionNode *node = session_list_head;
        session_list_head = node->next;
        free (node);
    }
    session_list_tail = NULL;
    session_count = 0;
    pthread_mutex_unlock (&session_mutex);
}

int
hev_socks5_tunnel_run (void)
{
    LOG_I ("starting socks5 tunnel");

    run = 1;

    /* Start timer thread */
    if (pthread_create (&timer_thread, NULL, timer_thread_func, NULL) != 0) {
        LOG_E ("failed to create timer thread");
        return -1;
    }

    /* Start tunnel I/O */
    if (hev_tunnel_io_start (tunnel_io) < 0) {
        LOG_E ("failed to start tunnel I/O");
        run = 0;
        pthread_join (timer_thread, NULL);
        return -1;
    }

    LOG_I ("socks5 tunnel running (press Ctrl+C to stop)");

    /* Wait for timer thread to finish */
    pthread_join (timer_thread, NULL);

    LOG_I ("socks5 tunnel stopped");
    return 0;
}

void
hev_socks5_tunnel_stop (void)
{
    LOG_I ("stopping socks5 tunnel");
    run = 0;

    if (tunnel_io)
        hev_tunnel_io_stop (tunnel_io);
}

void
hev_socks5_tunnel_stats (size_t *tx_packets, size_t *tx_bytes,
                         size_t *rx_packets, size_t *rx_bytes)
{
    if (tunnel_io) {
        hev_tunnel_io_get_stats (tunnel_io, tx_packets, tx_bytes, rx_packets,
                                rx_bytes);
    } else {
        if (tx_packets)
            *tx_packets = 0;
        if (tx_bytes)
            *tx_bytes = 0;
        if (rx_packets)
            *rx_packets = 0;
        if (rx_bytes)
            *rx_bytes = 0;
    }
}
