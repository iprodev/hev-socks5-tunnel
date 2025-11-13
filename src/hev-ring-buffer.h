/*
 ============================================================================
 Name        : hev-ring-buffer.h
 Author      : Performance Optimization Team
 Copyright   : Copyright (c) 2025
 Description : Lock-Free Ring Buffer (SPSC)
 ============================================================================
 */

#ifndef __HEV_RING_BUFFER_H__
#define __HEV_RING_BUFFER_H__

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>

#define RING_BUFFER_SIZE 4096
#define CACHE_LINE_SIZE 64

/* Lock-free Single-Producer-Single-Consumer Ring Buffer */
typedef struct _HevRingBuffer HevRingBuffer;

struct _HevRingBuffer {
    /* Producer cache line */
    _Alignas(CACHE_LINE_SIZE) _Atomic size_t head;
    size_t cached_tail;
    
    /* Consumer cache line */
    _Alignas(CACHE_LINE_SIZE) _Atomic size_t tail;
    size_t cached_head;
    
    /* Shared data */
    _Alignas(CACHE_LINE_SIZE) void *buffer[RING_BUFFER_SIZE];
    size_t mask;
};

/**
 * hev_ring_buffer_new:
 *
 * Create a new lock-free ring buffer
 *
 * Returns: HevRingBuffer pointer or NULL on error
 *
 * Since: 2.0
 */
HevRingBuffer *hev_ring_buffer_new(void);

/**
 * hev_ring_buffer_destroy:
 * @rb: HevRingBuffer
 *
 * Destroy ring buffer
 *
 * Since: 2.0
 */
void hev_ring_buffer_destroy(HevRingBuffer *rb);

/**
 * hev_ring_buffer_push:
 * @rb: HevRingBuffer
 * @data: pointer to push
 *
 * Push data to ring buffer (producer side)
 *
 * Returns: 0 on success, -1 if full
 *
 * Since: 2.0
 */
static inline int
hev_ring_buffer_push(HevRingBuffer *rb, void *data)
{
    size_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    size_t next = (head + 1) & rb->mask;
    
    /* Check if full (cache tail locally to reduce cache line ping-pong) */
    if (next == rb->cached_tail) {
        rb->cached_tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
        if (next == rb->cached_tail)
            return -1; /* Full */
    }
    
    rb->buffer[head] = data;
    atomic_store_explicit(&rb->head, next, memory_order_release);
    
    return 0;
}

/**
 * hev_ring_buffer_pop:
 * @rb: HevRingBuffer
 *
 * Pop data from ring buffer (consumer side)
 *
 * Returns: pointer or NULL if empty
 *
 * Since: 2.0
 */
static inline void *
hev_ring_buffer_pop(HevRingBuffer *rb)
{
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    
    /* Check if empty */
    if (tail == rb->cached_head) {
        rb->cached_head = atomic_load_explicit(&rb->head, memory_order_acquire);
        if (tail == rb->cached_head)
            return NULL; /* Empty */
    }
    
    void *data = rb->buffer[tail];
    size_t next = (tail + 1) & rb->mask;
    atomic_store_explicit(&rb->tail, next, memory_order_release);
    
    return data;
}

/**
 * hev_ring_buffer_size:
 * @rb: HevRingBuffer
 *
 * Get approximate number of items in buffer
 *
 * Returns: approximate size
 *
 * Since: 2.0
 */
static inline size_t
hev_ring_buffer_size(HevRingBuffer *rb)
{
    size_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    return (head - tail) & rb->mask;
}

/**
 * hev_ring_buffer_is_empty:
 * @rb: HevRingBuffer
 *
 * Check if buffer is empty
 *
 * Returns: true if empty
 *
 * Since: 2.0
 */
static inline bool
hev_ring_buffer_is_empty(HevRingBuffer *rb)
{
    return hev_ring_buffer_size(rb) == 0;
}

/**
 * hev_ring_buffer_is_full:
 * @rb: HevRingBuffer
 *
 * Check if buffer is full
 *
 * Returns: true if full
 *
 * Since: 2.0
 */
static inline bool
hev_ring_buffer_is_full(HevRingBuffer *rb)
{
    return hev_ring_buffer_size(rb) >= (RING_BUFFER_SIZE - 1);
}

#endif /* __HEV_RING_BUFFER_H__ */
