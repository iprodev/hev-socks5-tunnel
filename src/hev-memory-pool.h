/*
 ============================================================================
 Name        : hev-memory-pool.h
 Author      : Performance Optimization Team
 Copyright   : Copyright (c) 2025
 Description : Memory Pool for Packet Buffers
 ============================================================================
 */

#ifndef __HEV_MEMORY_POOL_H__
#define __HEV_MEMORY_POOL_H__

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>

#define POOL_MAX_BUFFERS 2048
#define POOL_BUFFER_SIZE 2048

/* Memory Pool for efficient buffer allocation */
typedef struct _HevMemoryPool HevMemoryPool;

struct _HevMemoryPool {
    void *buffers[POOL_MAX_BUFFERS];
    _Atomic uint32_t free_bitmap[64]; /* 2048 / 32 = 64 */
    size_t buffer_size;
    size_t buffer_count;
    _Atomic size_t allocated;
    _Atomic size_t peak_usage;
};

/**
 * hev_memory_pool_new:
 * @buffer_size: size of each buffer
 * @buffer_count: number of buffers
 *
 * Create new memory pool
 *
 * Returns: HevMemoryPool pointer or NULL
 *
 * Since: 2.0
 */
HevMemoryPool *hev_memory_pool_new(size_t buffer_size, size_t buffer_count);

/**
 * hev_memory_pool_destroy:
 * @pool: HevMemoryPool
 *
 * Destroy memory pool
 *
 * Since: 2.0
 */
void hev_memory_pool_destroy(HevMemoryPool *pool);

/**
 * hev_memory_pool_alloc:
 * @pool: HevMemoryPool
 *
 * Allocate buffer from pool
 *
 * Returns: buffer pointer or NULL if pool is empty
 *
 * Since: 2.0
 */
void *hev_memory_pool_alloc(HevMemoryPool *pool);

/**
 * hev_memory_pool_free:
 * @pool: HevMemoryPool
 * @ptr: pointer to free
 *
 * Free buffer back to pool
 *
 * Since: 2.0
 */
void hev_memory_pool_free(HevMemoryPool *pool, void *ptr);

/**
 * hev_memory_pool_get_stats:
 * @pool: HevMemoryPool
 * @allocated: (out) current allocated count
 * @peak: (out) peak usage
 *
 * Get pool statistics
 *
 * Since: 2.0
 */
void hev_memory_pool_get_stats(HevMemoryPool *pool, 
                               size_t *allocated, 
                               size_t *peak);

#endif /* __HEV_MEMORY_POOL_H__ */
