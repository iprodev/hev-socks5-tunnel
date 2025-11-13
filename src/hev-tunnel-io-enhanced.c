/*
 ============================================================================
 Name        : hev-tunnel-io-enhanced.c
 Author      : Performance Optimization Team  
 Copyright   : Copyright (c) 2025
 Description : Enhanced Tunnel I/O Implementation
 ============================================================================
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "hev-tunnel-io-enhanced.h"
#include "hev-memory-pool.h"

#ifdef __linux__
#include <sys/sendfile.h>
#endif

struct _HevTunnelIOEnhanced {
    int tun_fd;
    HevIOMode mode;
    HevMemoryPool *buffer_pool;
    HevTunnelIOStats stats;
    
    /* Batch buffers */
    void *batch_buffers[BATCH_SIZE];
    size_t batch_sizes[BATCH_SIZE];
    int batch_count;
    
    /* Pipe for splice */
    int pipe_fds[2];
};

HevTunnelIOEnhanced *
hev_tunnel_io_enhanced_new(int tun_fd, HevIOMode mode)
{
    HevTunnelIOEnhanced *io;
    
    if (tun_fd < 0)
        return NULL;
    
    io = calloc(1, sizeof(HevTunnelIOEnhanced));
    if (!io)
        return NULL;
    
    io->tun_fd = tun_fd;
    io->mode = mode;
    io->pipe_fds[0] = -1;
    io->pipe_fds[1] = -1;
    
    /* Create buffer pool */
    io->buffer_pool = hev_memory_pool_new(2048, 1024);
    if (!io->buffer_pool) {
        free(io);
        return NULL;
    }
    
    /* Create pipe for splice operations */
    if (mode == HEV_IO_MODE_ZEROCOPY) {
#ifdef __linux__
        if (pipe2(io->pipe_fds, O_NONBLOCK) < 0) {
            hev_memory_pool_destroy(io->buffer_pool);
            free(io);
            return NULL;
        }
#endif
    }
    
    return io;
}

void
hev_tunnel_io_enhanced_destroy(HevTunnelIOEnhanced *io)
{
    if (!io)
        return;
    
    if (io->buffer_pool)
        hev_memory_pool_destroy(io->buffer_pool);
    
    if (io->pipe_fds[0] >= 0)
        close(io->pipe_fds[0]);
    if (io->pipe_fds[1] >= 0)
        close(io->pipe_fds[1]);
    
    free(io);
}

int
hev_tunnel_io_enhanced_read_batch(HevTunnelIOEnhanced *io,
                                 void **buffers,
                                 size_t *sizes,
                                 int max_count)
{
    if (!io || !buffers || !sizes || max_count <= 0)
        return -1;
    
    int count = 0;
    
    for (int i = 0; i < max_count; i++) {
        /* Allocate buffer from pool */
        void *buf = hev_memory_pool_alloc(io->buffer_pool);
        if (!buf)
            break;
        
        /* Read packet */
        ssize_t n = read(io->tun_fd, buf, 2048);
        
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                hev_memory_pool_free(io->buffer_pool, buf);
                break;
            }
            hev_memory_pool_free(io->buffer_pool, buf);
            io->stats.errors++;
            break;
        }
        
        if (n == 0) {
            hev_memory_pool_free(io->buffer_pool, buf);
            break;
        }
        
        buffers[i] = buf;
        sizes[i] = n;
        count++;
        
        io->stats.packets_read++;
        io->stats.bytes_read += n;
    }
    
    if (count > 0)
        io->stats.batches_processed++;
    
    return count;
}

int
hev_tunnel_io_enhanced_write_batch(HevTunnelIOEnhanced *io,
                                  void **buffers,
                                  size_t *sizes,
                                  int count)
{
    if (!io || !buffers || !sizes || count <= 0)
        return -1;
    
    int written = 0;
    
    for (int i = 0; i < count; i++) {
        ssize_t n = write(io->tun_fd, buffers[i], sizes[i]);
        
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            io->stats.errors++;
            break;
        }
        
        io->stats.packets_written++;
        io->stats.bytes_written += n;
        written++;
        
        /* Free buffer back to pool */
        hev_memory_pool_free(io->buffer_pool, buffers[i]);
    }
    
    if (written > 0)
        io->stats.batches_processed++;
    
    return written;
}

ssize_t
hev_tunnel_io_enhanced_splice(HevTunnelIOEnhanced *io,
                             int dst_fd,
                             size_t len)
{
#ifdef __linux__
    if (!io || dst_fd < 0 || io->pipe_fds[0] < 0)
        return -1;
    
    /* Splice from tun to pipe */
    ssize_t n = splice(io->tun_fd, NULL, io->pipe_fds[1], NULL,
                      len, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
    
    if (n <= 0)
        return n;
    
    /* Splice from pipe to destination */
    ssize_t written = splice(io->pipe_fds[0], NULL, dst_fd, NULL,
                            n, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
    
    if (written > 0) {
        io->stats.bytes_read += written;
        io->stats.bytes_written += written;
        io->stats.zero_copy_operations++;
    }
    
    return written;
#else
    return -1;
#endif
}

ssize_t
hev_tunnel_io_enhanced_readv(HevTunnelIOEnhanced *io,
                            struct iovec *iov,
                            int iovcnt)
{
    if (!io || !iov || iovcnt <= 0)
        return -1;
    
    ssize_t n = readv(io->tun_fd, iov, iovcnt);
    
    if (n > 0) {
        io->stats.bytes_read += n;
        io->stats.packets_read++;
    } else if (n < 0) {
        io->stats.errors++;
    }
    
    return n;
}

ssize_t
hev_tunnel_io_enhanced_writev(HevTunnelIOEnhanced *io,
                             const struct iovec *iov,
                             int iovcnt)
{
    if (!io || !iov || iovcnt <= 0)
        return -1;
    
    ssize_t n = writev(io->tun_fd, iov, iovcnt);
    
    if (n > 0) {
        io->stats.bytes_written += n;
        io->stats.packets_written++;
    } else if (n < 0) {
        io->stats.errors++;
    }
    
    return n;
}

void
hev_tunnel_io_enhanced_get_stats(HevTunnelIOEnhanced *io,
                                HevTunnelIOStats *stats)
{
    if (!io || !stats)
        return;
    
    memcpy(stats, &io->stats, sizeof(HevTunnelIOStats));
}

void
hev_tunnel_io_enhanced_reset_stats(HevTunnelIOEnhanced *io)
{
    if (!io)
        return;
    
    memset(&io->stats, 0, sizeof(HevTunnelIOStats));
}
