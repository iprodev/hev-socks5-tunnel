/*
 ============================================================================
 Name        : hev-io-uring.c
 Author      : Performance Optimization Team
 Copyright   : Copyright (c) 2025
 Description : io_uring Implementation
 ============================================================================
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "hev-io-uring.h"

#ifdef HEV_IO_URING_SUPPORTED
#include <liburing.h>

typedef struct {
    HevIoUringCallback callback;
    void *user_data;
} HevIoUringRequest;

struct _HevIoUring {
    struct io_uring ring;
    uint32_t entries;
};

HevIoUring *
hev_io_uring_new(uint32_t entries)
{
    HevIoUring *ring;
    int ret;
    
    ring = calloc(1, sizeof(HevIoUring));
    if (!ring)
        return NULL;
    
    ring->entries = entries;
    
    /* Initialize io_uring */
    ret = io_uring_queue_init(entries, &ring->ring, 0);
    if (ret < 0) {
        free(ring);
        return NULL;
    }
    
    return ring;
}

void
hev_io_uring_destroy(HevIoUring *ring)
{
    if (!ring)
        return;
    
    io_uring_queue_exit(&ring->ring);
    free(ring);
}

int
hev_io_uring_read(HevIoUring *ring,
                 int fd,
                 void *buf,
                 size_t len,
                 off_t offset,
                 HevIoUringCallback callback,
                 void *user_data)
{
    if (!ring)
        return -1;
    
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring->ring);
    if (!sqe)
        return -1;
    
    /* Prepare request data */
    HevIoUringRequest *req = malloc(sizeof(HevIoUringRequest));
    if (!req)
        return -1;
    
    req->callback = callback;
    req->user_data = user_data;
    
    /* Prepare read operation */
    if (offset >= 0)
        io_uring_prep_read(sqe, fd, buf, len, offset);
    else
        io_uring_prep_read(sqe, fd, buf, len, 0);
    
    io_uring_sqe_set_data(sqe, req);
    
    return 0;
}

int
hev_io_uring_write(HevIoUring *ring,
                  int fd,
                  const void *buf,
                  size_t len,
                  off_t offset,
                  HevIoUringCallback callback,
                  void *user_data)
{
    if (!ring)
        return -1;
    
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring->ring);
    if (!sqe)
        return -1;
    
    HevIoUringRequest *req = malloc(sizeof(HevIoUringRequest));
    if (!req)
        return -1;
    
    req->callback = callback;
    req->user_data = user_data;
    
    if (offset >= 0)
        io_uring_prep_write(sqe, fd, buf, len, offset);
    else
        io_uring_prep_write(sqe, fd, buf, len, 0);
    
    io_uring_sqe_set_data(sqe, req);
    
    return 0;
}

int
hev_io_uring_submit(HevIoUring *ring)
{
    if (!ring)
        return -1;
    
    return io_uring_submit(&ring->ring);
}

int
hev_io_uring_wait(HevIoUring *ring, uint32_t min_complete)
{
    if (!ring)
        return -1;
    
    struct io_uring_cqe *cqe;
    int count = 0;
    
    /* Wait for at least min_complete completions */
    int ret = io_uring_wait_cqe_nr(&ring->ring, &cqe, min_complete);
    if (ret < 0)
        return ret;
    
    /* Process all available completions */
    unsigned head;
    io_uring_for_each_cqe(&ring->ring, head, cqe) {
        HevIoUringRequest *req = io_uring_cqe_get_data(cqe);
        
        if (req) {
            if (req->callback)
                req->callback(req->user_data, cqe->res);
            free(req);
        }
        
        count++;
    }
    
    io_uring_cq_advance(&ring->ring, count);
    
    return count;
}

int
hev_io_uring_supported(void)
{
    /* Try to create a small ring to test support */
    struct io_uring ring;
    int ret = io_uring_queue_init(2, &ring, 0);
    
    if (ret == 0) {
        io_uring_queue_exit(&ring);
        return 1;
    }
    
    return 0;
}

#else /* !HEV_IO_URING_SUPPORTED */

HevIoUring *
hev_io_uring_new(uint32_t entries)
{
    return NULL;
}

void
hev_io_uring_destroy(HevIoUring *ring)
{
}

int
hev_io_uring_read(HevIoUring *ring, int fd, void *buf, size_t len,
                 off_t offset, HevIoUringCallback callback, void *user_data)
{
    return -1;
}

int
hev_io_uring_write(HevIoUring *ring, int fd, const void *buf, size_t len,
                  off_t offset, HevIoUringCallback callback, void *user_data)
{
    return -1;
}

int
hev_io_uring_submit(HevIoUring *ring)
{
    return -1;
}

int
hev_io_uring_wait(HevIoUring *ring, uint32_t min_complete)
{
    return -1;
}

int
hev_io_uring_supported(void)
{
    return 0;
}

#endif /* HEV_IO_URING_SUPPORTED */
