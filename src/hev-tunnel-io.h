/*
 ============================================================================
 Name        : hev-tunnel-io.h
 Author      : Enhanced Multi-threading Support
 Description : Multi-threaded TUN I/O Management
 ============================================================================
 */

#ifndef __HEV_TUNNEL_IO_H__
#define __HEV_TUNNEL_IO_H__

#include <lwip/pbuf.h>

typedef struct _HevTunnelIO HevTunnelIO;

/**
 * hev_tunnel_io_new:
 * @tun_fd: tunnel file descriptor
 * @mtu: maximum transmission unit
 *
 * Create a new multi-threaded tunnel I/O manager.
 *
 * Returns: new tunnel I/O instance
 */
HevTunnelIO *hev_tunnel_io_new (int tun_fd, unsigned int mtu);

/**
 * hev_tunnel_io_destroy:
 * @io: tunnel I/O instance
 *
 * Destroy the tunnel I/O manager and stop all threads.
 */
void hev_tunnel_io_destroy (HevTunnelIO *io);

/**
 * hev_tunnel_io_start:
 * @io: tunnel I/O instance
 *
 * Start the I/O threads.
 *
 * Returns: 0 on success, -1 on failure
 */
int hev_tunnel_io_start (HevTunnelIO *io);

/**
 * hev_tunnel_io_stop:
 * @io: tunnel I/O instance
 *
 * Stop the I/O threads gracefully.
 */
void hev_tunnel_io_stop (HevTunnelIO *io);

/**
 * hev_tunnel_io_write:
 * @io: tunnel I/O instance
 * @buf: packet buffer to write
 *
 * Queue a packet for writing to the tunnel.
 *
 * Returns: 0 on success, -1 on failure
 */
int hev_tunnel_io_write (HevTunnelIO *io, struct pbuf *buf);

/**
 * hev_tunnel_io_set_read_callback:
 * @io: tunnel I/O instance
 * @callback: function to call when packet is read
 * @user_data: data to pass to callback
 *
 * Set the callback for received packets.
 */
void hev_tunnel_io_set_read_callback (HevTunnelIO *io,
                                      void (*callback) (struct pbuf *, void *),
                                      void *user_data);

/**
 * hev_tunnel_io_get_stats:
 * @io: tunnel I/O instance
 * @tx_packets: transmitted packets (out)
 * @tx_bytes: transmitted bytes (out)
 * @rx_packets: received packets (out)
 * @rx_bytes: received bytes (out)
 *
 * Get tunnel I/O statistics.
 */
void hev_tunnel_io_get_stats (HevTunnelIO *io, size_t *tx_packets,
                              size_t *tx_bytes, size_t *rx_packets,
                              size_t *rx_bytes);

#endif /* __HEV_TUNNEL_IO_H__ */
