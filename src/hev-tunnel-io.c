/*
 ============================================================================
 Name        : hev-tunnel-io.c
 Author      : Enhanced Multi-threading Support
 Description : Multi-threaded TUN I/O Implementation
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/uio.h>

#include <lwip/pbuf.h>

#include "hev-tunnel-io.h"
#include "hev-tunnel.h"
#include "hev-logger.h"

#define WRITE_QUEUE_SIZE 4096
#define READ_BATCH_SIZE 32
#define WRITE_BATCH_SIZE 16

typedef struct _HevPacketNode HevPacketNode;

struct _HevPacketNode
{
    struct pbuf *packet;
    HevPacketNode *next;
};

struct _HevTunnelIO
{
    int tun_fd;
    unsigned int mtu;
    int running;

    /* Reader threads */
    pthread_t *reader_threads;
    int num_readers;

    /* Writer threads */
    pthread_t *writer_threads;
    int num_writers;

    /* Write queue */
    HevPacketNode *write_queue_head;
    HevPacketNode *write_queue_tail;
    int write_queue_size;
    pthread_mutex_t write_mutex;
    pthread_cond_t write_cond;

    /* Read callback */
    void (*read_callback) (struct pbuf *, void *);
    void *callback_data;
    pthread_mutex_t callback_mutex;

    /* Statistics (atomic) */
    volatile size_t tx_packets;
    volatile size_t tx_bytes;
    volatile size_t rx_packets;
    volatile size_t rx_bytes;
};

static void *
reader_thread (void *arg)
{
    HevTunnelIO *io = (HevTunnelIO *)arg;
    unsigned char *buffer;

    buffer = (unsigned char *)malloc (io->mtu + 4);
    if (!buffer) {
        LOG_E ("tunnel io: failed to allocate read buffer");
        return NULL;
    }

    LOG_D ("tunnel io: reader thread started");

    while (io->running) {
        struct pbuf *pbuf;
        ssize_t n;

        /* Read packet */
        n = read (io->tun_fd, buffer, io->mtu + 4);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep (100);
                continue;
            }
            if (errno == EINTR)
                continue;
            LOG_E ("tunnel io: read error: %s", strerror (errno));
            break;
        }

        if (n == 0)
            continue;

        /* Allocate pbuf */
        pbuf = pbuf_alloc (PBUF_RAW, n, PBUF_RAM);
        if (!pbuf) {
            LOG_W ("tunnel io: failed to allocate pbuf");
            continue;
        }

        /* Copy data */
        memcpy (pbuf->payload, buffer, n);

        /* Update stats */
        __sync_fetch_and_add (&io->rx_packets, 1);
        __sync_fetch_and_add (&io->rx_bytes, n);

        /* Call callback */
        pthread_mutex_lock (&io->callback_mutex);
        if (io->read_callback)
            io->read_callback (pbuf, io->callback_data);
        else
            pbuf_free (pbuf);
        pthread_mutex_unlock (&io->callback_mutex);
    }

    free (buffer);
    LOG_D ("tunnel io: reader thread stopped");
    return NULL;
}

static void *
writer_thread (void *arg)
{
    HevTunnelIO *io = (HevTunnelIO *)arg;
    HevPacketNode *batch[WRITE_BATCH_SIZE];
    int batch_count;

    LOG_D ("tunnel io: writer thread started");

    while (io->running || io->write_queue_size > 0) {
        batch_count = 0;

        /* Get batch of packets */
        pthread_mutex_lock (&io->write_mutex);

        while (io->write_queue_size == 0 && io->running) {
            struct timespec ts;
            clock_gettime (CLOCK_REALTIME, &ts);
            ts.tv_nsec += 1000000; /* 1ms timeout */
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait (&io->write_cond, &io->write_mutex, &ts);
        }

        while (batch_count < WRITE_BATCH_SIZE && io->write_queue_head) {
            batch[batch_count++] = io->write_queue_head;
            io->write_queue_head = io->write_queue_head->next;
            io->write_queue_size--;
        }

        if (io->write_queue_head == NULL)
            io->write_queue_tail = NULL;

        pthread_mutex_unlock (&io->write_mutex);

        /* Write batch */
        for (int i = 0; i < batch_count; i++) {
            HevPacketNode *node = batch[i];
            struct pbuf *p = node->packet;
            ssize_t written;

            /* Write packet */
            if (p->len == p->tot_len) {
                written = write (io->tun_fd, p->payload, p->len);
            } else {
                /* Handle chained pbufs */
                unsigned char *buffer = malloc (p->tot_len);
                if (buffer) {
                    struct pbuf *q;
                    unsigned char *ptr = buffer;
                    for (q = p; q != NULL; q = q->next) {
                        memcpy (ptr, q->payload, q->len);
                        ptr += q->len;
                    }
                    written = write (io->tun_fd, buffer, p->tot_len);
                    free (buffer);
                } else {
                    written = -1;
                }
            }

            if (written > 0) {
                __sync_fetch_and_add (&io->tx_packets, 1);
                __sync_fetch_and_add (&io->tx_bytes, written);
            } else if (written < 0 && errno != EAGAIN && errno != EINTR) {
                LOG_W ("tunnel io: write error: %s", strerror (errno));
            }

            pbuf_free (p);
            free (node);
        }
    }

    LOG_D ("tunnel io: writer thread stopped");
    return NULL;
}

HevTunnelIO *
hev_tunnel_io_new (int tun_fd, unsigned int mtu)
{
    HevTunnelIO *io;
    int num_cpus;

    io = (HevTunnelIO *)calloc (1, sizeof (HevTunnelIO));
    if (!io)
        return NULL;

    io->tun_fd = tun_fd;
    io->mtu = mtu;

    pthread_mutex_init (&io->write_mutex, NULL);
    pthread_cond_init (&io->write_cond, NULL);
    pthread_mutex_init (&io->callback_mutex, NULL);

    /* Auto-detect thread counts */
#ifdef __linux__
    num_cpus = sysconf (_SC_NPROCESSORS_ONLN);
#else
    num_cpus = 4;
#endif
    if (num_cpus < 2)
        num_cpus = 2;

    /* Use 2 readers and 2 writers by default */
    io->num_readers = (num_cpus >= 4) ? 2 : 1;
    io->num_writers = (num_cpus >= 4) ? 2 : 1;

    io->reader_threads =
        (pthread_t *)calloc (io->num_readers, sizeof (pthread_t));
    io->writer_threads =
        (pthread_t *)calloc (io->num_writers, sizeof (pthread_t));

    if (!io->reader_threads || !io->writer_threads) {
        hev_tunnel_io_destroy (io);
        return NULL;
    }

    LOG_I ("tunnel io: created with %d readers, %d writers", io->num_readers,
           io->num_writers);

    return io;
}

void
hev_tunnel_io_destroy (HevTunnelIO *io)
{
    if (!io)
        return;

    hev_tunnel_io_stop (io);

    /* Clean up write queue */
    while (io->write_queue_head) {
        HevPacketNode *node = io->write_queue_head;
        io->write_queue_head = node->next;
        if (node->packet)
            pbuf_free (node->packet);
        free (node);
    }

    pthread_mutex_destroy (&io->write_mutex);
    pthread_cond_destroy (&io->write_cond);
    pthread_mutex_destroy (&io->callback_mutex);

    free (io->reader_threads);
    free (io->writer_threads);
    free (io);
}

int
hev_tunnel_io_start (HevTunnelIO *io)
{
    int i;

    if (!io || io->running)
        return -1;

    io->running = 1;

    /* Start reader threads */
    for (i = 0; i < io->num_readers; i++) {
        if (pthread_create (&io->reader_threads[i], NULL, reader_thread, io) !=
            0) {
            LOG_E ("tunnel io: failed to create reader thread");
            io->running = 0;
            return -1;
        }
    }

    /* Start writer threads */
    for (i = 0; i < io->num_writers; i++) {
        if (pthread_create (&io->writer_threads[i], NULL, writer_thread, io) !=
            0) {
            LOG_E ("tunnel io: failed to create writer thread");
            io->running = 0;
            return -1;
        }
    }

    LOG_I ("tunnel io: started");
    return 0;
}

void
hev_tunnel_io_stop (HevTunnelIO *io)
{
    int i;

    if (!io || !io->running)
        return;

    io->running = 0;

    /* Wake up writers */
    pthread_cond_broadcast (&io->write_cond);

    /* Wait for reader threads */
    for (i = 0; i < io->num_readers; i++) {
        pthread_join (io->reader_threads[i], NULL);
    }

    /* Wait for writer threads */
    for (i = 0; i < io->num_writers; i++) {
        pthread_join (io->writer_threads[i], NULL);
    }

    LOG_I ("tunnel io: stopped");
}

int
hev_tunnel_io_write (HevTunnelIO *io, struct pbuf *buf)
{
    HevPacketNode *node;

    if (!io || !buf)
        return -1;

    node = (HevPacketNode *)malloc (sizeof (HevPacketNode));
    if (!node)
        return -1;

    /* Reference the pbuf */
    pbuf_ref (buf);
    node->packet = buf;
    node->next = NULL;

    pthread_mutex_lock (&io->write_mutex);

    /* Check queue size */
    if (io->write_queue_size >= WRITE_QUEUE_SIZE) {
        pthread_mutex_unlock (&io->write_mutex);
        pbuf_free (buf);
        free (node);
        LOG_W ("tunnel io: write queue full");
        return -1;
    }

    /* Add to queue */
    if (io->write_queue_tail) {
        io->write_queue_tail->next = node;
        io->write_queue_tail = node;
    } else {
        io->write_queue_head = node;
        io->write_queue_tail = node;
    }
    io->write_queue_size++;

    /* Signal writers */
    pthread_cond_signal (&io->write_cond);
    pthread_mutex_unlock (&io->write_mutex);

    return 0;
}

void
hev_tunnel_io_set_read_callback (HevTunnelIO *io,
                                 void (*callback) (struct pbuf *, void *),
                                 void *user_data)
{
    if (!io)
        return;

    pthread_mutex_lock (&io->callback_mutex);
    io->read_callback = callback;
    io->callback_data = user_data;
    pthread_mutex_unlock (&io->callback_mutex);
}

void
hev_tunnel_io_get_stats (HevTunnelIO *io, size_t *tx_packets, size_t *tx_bytes,
                         size_t *rx_packets, size_t *rx_bytes)
{
    if (!io)
        return;

    if (tx_packets)
        *tx_packets = io->tx_packets;
    if (tx_bytes)
        *tx_bytes = io->tx_bytes;
    if (rx_packets)
        *rx_packets = io->rx_packets;
    if (rx_bytes)
        *rx_bytes = io->rx_bytes;
}
