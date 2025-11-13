/*
 ============================================================================
 Name        : hev-connection-pool.h
 Author      : Performance Optimization Team
 Copyright   : Copyright (c) 2025
 Description : Connection Pool for SOCKS5
 ============================================================================
 */

#ifndef __HEV_CONNECTION_POOL_H__
#define __HEV_CONNECTION_POOL_H__

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>

#define CONN_POOL_SIZE 128
#define CONN_IDLE_TIMEOUT 60

typedef struct _HevConnection HevConnection;
typedef struct _HevConnectionPool HevConnectionPool;

struct _HevConnection {
    int fd;
    time_t last_used;
    time_t created;
    bool in_use;
    uint32_t use_count;
};

struct _HevConnectionPool {
    HevConnection conns[CONN_POOL_SIZE];
    pthread_mutex_t mutex;
    size_t size;
    
    /* Statistics */
    uint64_t total_requests;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t evictions;
};

/**
 * hev_connection_pool_new:
 * @size: pool size
 *
 * Create new connection pool
 *
 * Returns: HevConnectionPool or NULL
 *
 * Since: 2.0
 */
HevConnectionPool *hev_connection_pool_new(size_t size);

/**
 * hev_connection_pool_destroy:
 * @pool: HevConnectionPool
 *
 * Destroy connection pool
 *
 * Since: 2.0
 */
void hev_connection_pool_destroy(HevConnectionPool *pool);

/**
 * hev_connection_pool_get:
 * @pool: HevConnectionPool
 * @server: server address
 * @port: server port
 *
 * Get connection from pool or create new
 *
 * Returns: file descriptor or -1
 *
 * Since: 2.0
 */
int hev_connection_pool_get(HevConnectionPool *pool, 
                           const char *server, 
                           uint16_t port);

/**
 * hev_connection_pool_release:
 * @pool: HevConnectionPool
 * @fd: file descriptor
 *
 * Release connection back to pool
 *
 * Since: 2.0
 */
void hev_connection_pool_release(HevConnectionPool *pool, int fd);

/**
 * hev_connection_pool_remove:
 * @pool: HevConnectionPool
 * @fd: file descriptor
 *
 * Remove connection from pool (on error)
 *
 * Since: 2.0
 */
void hev_connection_pool_remove(HevConnectionPool *pool, int fd);

/**
 * hev_connection_pool_cleanup:
 * @pool: HevConnectionPool
 *
 * Clean up idle connections
 *
 * Since: 2.0
 */
void hev_connection_pool_cleanup(HevConnectionPool *pool);

/**
 * hev_connection_pool_stats:
 * @pool: HevConnectionPool
 * @hits: (out) cache hits
 * @misses: (out) cache misses
 * @hit_rate: (out) hit rate percentage
 *
 * Get pool statistics
 *
 * Since: 2.0
 */
void hev_connection_pool_stats(HevConnectionPool *pool,
                              uint64_t *hits,
                              uint64_t *misses,
                              float *hit_rate);

#endif /* __HEV_CONNECTION_POOL_H__ */
