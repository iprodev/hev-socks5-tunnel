/*
 ============================================================================
 Name        : hev-ring-buffer.c
 Author      : Performance Optimization Team
 Copyright   : Copyright (c) 2025
 Description : Lock-Free Ring Buffer Implementation
 ============================================================================
 */

#include <stdlib.h>
#include <string.h>

#include "hev-ring-buffer.h"

HevRingBuffer *
hev_ring_buffer_new(void)
{
    HevRingBuffer *rb;
    
    rb = aligned_alloc(CACHE_LINE_SIZE, sizeof(HevRingBuffer));
    if (!rb)
        return NULL;
    
    memset(rb, 0, sizeof(HevRingBuffer));
    
    atomic_init(&rb->head, 0);
    atomic_init(&rb->tail, 0);
    rb->cached_head = 0;
    rb->cached_tail = 0;
    rb->mask = RING_BUFFER_SIZE - 1;
    
    return rb;
}

void
hev_ring_buffer_destroy(HevRingBuffer *rb)
{
    if (rb)
        free(rb);
}
