/*
 ============================================================================
 Name        : hev-tunnel-io-enhanced.h
 Author      : Performance Optimization Team
 Copyright   : Copyright (c) 2025
 Description : Enhanced Tunnel I/O with Batching and Zero-Copy
 ============================================================================
 */

#ifndef __HEV_TUNNEL_IO_ENHANCED_H__
#define __HEV_TUNNEL_IO_ENHANCED_H__

#include <stddef.h>
#include <stdint.h>
#include <sys/uio.h>

#define BATCH_SIZE 32
#define MAX_IOV 16

typedef struct _HevTunnelIOEnhanced HevTunnelIOEnhanced;

/* I/O mode */
typedef enum {
    HEV_IO_MODE_STANDARD,  /* Regular read/write */
    HEV_IO_MODE_BATCH,     /* Batch processing */
    HEV_IO_MODE_ZEROCOPY,  /* Zero-copy (splice/sendfile) */
    HEV_IO_MODE_IOV        /* Vectored I/O (readv/writev) */
} HevIOMode;

/* Statistics */
typedef struct {
    uint64_t bytes_read;
    uint64_t bytes_written;
    uint64_t packets_read;
    uint64_t packets_written;
    uint64_t batches_processed;
    uint64_t zero_copy_operations;
    uint64_t errors;
} HevTunnelIOStats;

/**
 * hev_tunnel_io_enhanced_new:
 * @tun_fd: tunnel file descriptor
 * @mode: I/O mode
 *
 * Create enhanced tunnel I/O
 *
 * Returns: HevTunnelIOEnhanced or NULL
 *
 * Since: 2.0
 */
HevTunnelIOEnhanced *hev_tunnel_io_enhanced_new(int tun_fd, HevIOMode mode);

/**
 * hev_tunnel_io_enhanced_destroy:
 * @io: HevTunnelIOEnhanced
 *
 * Destroy enhanced I/O
 *
 * Since: 2.0
 */
void hev_tunnel_io_enhanced_destroy(HevTunnelIOEnhanced *io);

/**
 * hev_tunnel_io_enhanced_read_batch:
 * @io: HevTunnelIOEnhanced
 * @buffers: array of buffer pointers (output)
 * @sizes: array of sizes (output)
 * @max_count: maximum buffers to read
 *
 * Read multiple packets in one batch
 *
 * Returns: number of packets read
 *
 * Since: 2.0
 */
int hev_tunnel_io_enhanced_read_batch(HevTunnelIOEnhanced *io,
                                     void **buffers,
                                     size_t *sizes,
                                     int max_count);

/**
 * hev_tunnel_io_enhanced_write_batch:
 * @io: HevTunnelIOEnhanced
 * @buffers: array of buffer pointers
 * @sizes: array of sizes
 * @count: number of buffers
 *
 * Write multiple packets in one batch
 *
 * Returns: number of packets written
 *
 * Since: 2.0
 */
int hev_tunnel_io_enhanced_write_batch(HevTunnelIOEnhanced *io,
                                      void **buffers,
                                      size_t *sizes,
                                      int count);

/**
 * hev_tunnel_io_enhanced_splice:
 * @io: HevTunnelIOEnhanced
 * @dst_fd: destination file descriptor
 * @len: length to splice
 *
 * Zero-copy splice operation
 *
 * Returns: bytes spliced or -1
 *
 * Since: 2.0
 */
ssize_t hev_tunnel_io_enhanced_splice(HevTunnelIOEnhanced *io,
                                     int dst_fd,
                                     size_t len);

/**
 * hev_tunnel_io_enhanced_readv:
 * @io: HevTunnelIOEnhanced
 * @iov: iovec array
 * @iovcnt: number of vectors
 *
 * Vectored read operation
 *
 * Returns: bytes read or -1
 *
 * Since: 2.0
 */
ssize_t hev_tunnel_io_enhanced_readv(HevTunnelIOEnhanced *io,
                                    struct iovec *iov,
                                    int iovcnt);

/**
 * hev_tunnel_io_enhanced_writev:
 * @io: HevTunnelIOEnhanced
 * @iov: iovec array
 * @iovcnt: number of vectors
 *
 * Vectored write operation
 *
 * Returns: bytes written or -1
 *
 * Since: 2.0
 */
ssize_t hev_tunnel_io_enhanced_writev(HevTunnelIOEnhanced *io,
                                     const struct iovec *iov,
                                     int iovcnt);

/**
 * hev_tunnel_io_enhanced_get_stats:
 * @io: HevTunnelIOEnhanced
 * @stats: (out) statistics structure
 *
 * Get I/O statistics
 *
 * Since: 2.0
 */
void hev_tunnel_io_enhanced_get_stats(HevTunnelIOEnhanced *io,
                                     HevTunnelIOStats *stats);

/**
 * hev_tunnel_io_enhanced_reset_stats:
 * @io: HevTunnelIOEnhanced
 *
 * Reset statistics
 *
 * Since: 2.0
 */
void hev_tunnel_io_enhanced_reset_stats(HevTunnelIOEnhanced *io);

#endif /* __HEV_TUNNEL_IO_ENHANCED_H__ */
