/*
 ============================================================================
 Name        : hev-connection-pool.c
 Author      : Performance Optimization Team
 Copyright   : Copyright (c) 2025
 Description : Connection Pool Implementation
 ============================================================================
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "hev-connection-pool.h"

HevConnectionPool *
hev_connection_pool_new(size_t size)
{
    HevConnectionPool *pool;
    
    if (size > CONN_POOL_SIZE)
        size = CONN_POOL_SIZE;
    
    pool = calloc(1, sizeof(HevConnectionPool));
    if (!pool)
        return NULL;
    
    pool->size = size;
    pthread_mutex_init(&pool->mutex, NULL);
    
    for (size_t i = 0; i < size; i++) {
        pool->conns[i].fd = -1;
        pool->conns[i].in_use = false;
    }
    
    return pool;
}

void
hev_connection_pool_destroy(HevConnectionPool *pool)
{
    if (!pool)
        return;
    
    pthread_mutex_lock(&pool->mutex);
    
    for (size_t i = 0; i < pool->size; i++) {
        if (pool->conns[i].fd >= 0) {
            close(pool->conns[i].fd);
            pool->conns[i].fd = -1;
        }
    }
    
    pthread_mutex_unlock(&pool->mutex);
    pthread_mutex_destroy(&pool->mutex);
    
    free(pool);
}

int
hev_connection_pool_get(HevConnectionPool *pool, 
                       const char *server, 
                       uint16_t port)
{
    if (!pool)
        return -1;
    
    time_t now = time(NULL);
    int fd = -1;
    
    pthread_mutex_lock(&pool->mutex);
    pool->total_requests++;
    
    /* Try to find existing connection */
    for (size_t i = 0; i < pool->size; i++) {
        if (pool->conns[i].fd >= 0 && 
            !pool->conns[i].in_use &&
            (now - pool->conns[i].last_used) < CONN_IDLE_TIMEOUT) {
            
            /* Test if connection is still alive */
            char buf[1];
            int ret = recv(pool->conns[i].fd, buf, 1, 
                          MSG_PEEK | MSG_DONTWAIT);
            
            if (ret == 0 || (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                /* Connection closed, remove it */
                close(pool->conns[i].fd);
                pool->conns[i].fd = -1;
                pool->evictions++;
                continue;
            }
            
            /* Reuse this connection */
            pool->conns[i].in_use = true;
            pool->conns[i].last_used = now;
            pool->conns[i].use_count++;
            fd = pool->conns[i].fd;
            pool->cache_hits++;
            break;
        }
    }
    
    pthread_mutex_unlock(&pool->mutex);
    
    /* Create new connection if none available */
    if (fd < 0) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;
        
        /* Set non-blocking */
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        
        /* Connect to server */
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, server, &addr.sin_addr);
        
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            if (errno != EINPROGRESS) {
                close(fd);
                return -1;
            }
        }
        
        pthread_mutex_lock(&pool->mutex);
        pool->cache_misses++;
        pthread_mutex_unlock(&pool->mutex);
    }
    
    return fd;
}

void
hev_connection_pool_release(HevConnectionPool *pool, int fd)
{
    if (!pool || fd < 0)
        return;
    
    time_t now = time(NULL);
    
    pthread_mutex_lock(&pool->mutex);
    
    /* Find connection in pool */
    bool found = false;
    for (size_t i = 0; i < pool->size; i++) {
        if (pool->conns[i].fd == fd) {
            pool->conns[i].in_use = false;
            pool->conns[i].last_used = now;
            found = true;
            break;
        }
    }
    
    /* Add to pool if not found */
    if (!found) {
        for (size_t i = 0; i < pool->size; i++) {
            if (pool->conns[i].fd < 0) {
                pool->conns[i].fd = fd;
                pool->conns[i].in_use = false;
                pool->conns[i].last_used = now;
                pool->conns[i].created = now;
                pool->conns[i].use_count = 1;
                found = true;
                break;
            }
        }
        
        /* Pool is full, close this connection */
        if (!found) {
            close(fd);
            pool->evictions++;
        }
    }
    
    pthread_mutex_unlock(&pool->mutex);
}

void
hev_connection_pool_remove(HevConnectionPool *pool, int fd)
{
    if (!pool || fd < 0)
        return;
    
    pthread_mutex_lock(&pool->mutex);
    
    for (size_t i = 0; i < pool->size; i++) {
        if (pool->conns[i].fd == fd) {
            close(pool->conns[i].fd);
            pool->conns[i].fd = -1;
            pool->conns[i].in_use = false;
            pool->evictions++;
            break;
        }
    }
    
    pthread_mutex_unlock(&pool->mutex);
}

void
hev_connection_pool_cleanup(HevConnectionPool *pool)
{
    if (!pool)
        return;
    
    time_t now = time(NULL);
    
    pthread_mutex_lock(&pool->mutex);
    
    for (size_t i = 0; i < pool->size; i++) {
        if (pool->conns[i].fd >= 0 &&
            !pool->conns[i].in_use &&
            (now - pool->conns[i].last_used) >= CONN_IDLE_TIMEOUT) {
            
            close(pool->conns[i].fd);
            pool->conns[i].fd = -1;
            pool->evictions++;
        }
    }
    
    pthread_mutex_unlock(&pool->mutex);
}

void
hev_connection_pool_stats(HevConnectionPool *pool,
                         uint64_t *hits,
                         uint64_t *misses,
                         float *hit_rate)
{
    if (!pool)
        return;
    
    pthread_mutex_lock(&pool->mutex);
    
    if (hits)
        *hits = pool->cache_hits;
    
    if (misses)
        *misses = pool->cache_misses;
    
    if (hit_rate) {
        if (pool->total_requests > 0)
            *hit_rate = (float)pool->cache_hits * 100.0f / pool->total_requests;
        else
            *hit_rate = 0.0f;
    }
    
    pthread_mutex_unlock(&pool->mutex);
}
