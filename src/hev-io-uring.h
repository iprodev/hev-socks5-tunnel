/*
 ============================================================================
 Name        : hev-io-uring.h
 Author      : Performance Optimization Team
 Copyright   : Copyright (c) 2025
 Description : io_uring Async I/O Support (Linux 5.1+)
 ============================================================================
 */

#ifndef __HEV_IO_URING_H__
#define __HEV_IO_URING_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __linux__
#define HEV_IO_URING_SUPPORTED 1
#endif

typedef struct _HevIoUring HevIoUring;
typedef void (*HevIoUringCallback)(void *user_data, int result);

/**
 * hev_io_uring_new:
 * @entries: queue depth
 *
 * Create new io_uring instance
 *
 * Returns: HevIoUring or NULL
 *
 * Since: 2.0
 */
HevIoUring *hev_io_uring_new(uint32_t entries);

/**
 * hev_io_uring_destroy:
 * @ring: HevIoUring
 *
 * Destroy io_uring instance
 *
 * Since: 2.0
 */
void hev_io_uring_destroy(HevIoUring *ring);

/**
 * hev_io_uring_read:
 * @ring: HevIoUring
 * @fd: file descriptor
 * @buf: buffer
 * @len: length
 * @offset: file offset (-1 for current)
 * @callback: completion callback
 * @user_data: user data for callback
 *
 * Submit async read operation
 *
 * Returns: 0 on success, -1 on error
 *
 * Since: 2.0
 */
int hev_io_uring_read(HevIoUring *ring, 
                     int fd, 
                     void *buf, 
                     size_t len,
                     off_t offset,
                     HevIoUringCallback callback,
                     void *user_data);

/**
 * hev_io_uring_write:
 * @ring: HevIoUring
 * @fd: file descriptor
 * @buf: buffer
 * @len: length
 * @offset: file offset (-1 for current)
 * @callback: completion callback
 * @user_data: user data for callback
 *
 * Submit async write operation
 *
 * Returns: 0 on success, -1 on error
 *
 * Since: 2.0
 */
int hev_io_uring_write(HevIoUring *ring,
                      int fd,
                      const void *buf,
                      size_t len,
                      off_t offset,
                      HevIoUringCallback callback,
                      void *user_data);

/**
 * hev_io_uring_submit:
 * @ring: HevIoUring
 *
 * Submit all pending operations
 *
 * Returns: number of operations submitted
 *
 * Since: 2.0
 */
int hev_io_uring_submit(HevIoUring *ring);

/**
 * hev_io_uring_wait:
 * @ring: HevIoUring
 * @min_complete: minimum completions to wait for
 *
 * Wait for completions and invoke callbacks
 *
 * Returns: number of completions processed
 *
 * Since: 2.0
 */
int hev_io_uring_wait(HevIoUring *ring, uint32_t min_complete);

/**
 * hev_io_uring_supported:
 *
 * Check if io_uring is supported
 *
 * Returns: 1 if supported, 0 otherwise
 *
 * Since: 2.0
 */
int hev_io_uring_supported(void);

#endif /* __HEV_IO_URING_H__ */
