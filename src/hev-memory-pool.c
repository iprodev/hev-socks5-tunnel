/*
 ============================================================================
 Name        : hev-memory-pool.c
 Author      : Performance Optimization Team
 Copyright   : Copyright (c) 2025
 Description : Memory Pool Implementation
 ============================================================================
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "hev-memory-pool.h"

#define CACHE_LINE_SIZE 64

HevMemoryPool *
hev_memory_pool_new(size_t buffer_size, size_t buffer_count)
{
    HevMemoryPool *pool;
    
    if (buffer_count > POOL_MAX_BUFFERS)
        buffer_count = POOL_MAX_BUFFERS;
    
    pool = aligned_alloc(CACHE_LINE_SIZE, sizeof(HevMemoryPool));
    if (!pool)
        return NULL;
    
    memset(pool, 0, sizeof(HevMemoryPool));
    
    pool->buffer_size = buffer_size;
    pool->buffer_count = buffer_count;
    
    /* Pre-allocate all buffers (cache-line aligned) */
    for (size_t i = 0; i < buffer_count; i++) {
        pool->buffers[i] = aligned_alloc(CACHE_LINE_SIZE, buffer_size);
        if (!pool->buffers[i]) {
            hev_memory_pool_destroy(pool);
            return NULL;
        }
    }
    
    /* Initialize bitmap (all free = all 1s) */
    size_t bitmap_size = (buffer_count + 31) / 32;
    for (size_t i = 0; i < bitmap_size; i++) {
        uint32_t mask = 0xFFFFFFFF;
        if (i == bitmap_size - 1) {
            /* Last word might be partial */
            size_t remaining = buffer_count - (i * 32);
            if (remaining < 32)
                mask = (1U << remaining) - 1;
        }
        atomic_init(&pool->free_bitmap[i], mask);
    }
    
    atomic_init(&pool->allocated, 0);
    atomic_init(&pool->peak_usage, 0);
    
    return pool;
}

void
hev_memory_pool_destroy(HevMemoryPool *pool)
{
    if (!pool)
        return;
    
    for (size_t i = 0; i < pool->buffer_count; i++) {
        if (pool->buffers[i])
            free(pool->buffers[i]);
    }
    
    free(pool);
}

void *
hev_memory_pool_alloc(HevMemoryPool *pool)
{
    if (!pool)
        return NULL;
    
    /* Find free slot using atomic bit operations */
    size_t bitmap_size = (pool->buffer_count + 31) / 32;
    
    for (size_t i = 0; i < bitmap_size; i++) {
        uint32_t bitmap = atomic_load_explicit(&pool->free_bitmap[i], 
                                               memory_order_acquire);
        
        while (bitmap != 0) {
            /* Find first set bit (ffs - find first set) */
            int bit = __builtin_ffs(bitmap) - 1;
            uint32_t mask = 1U << bit;
            
            /* Try to claim this bit atomically */
            uint32_t expected = bitmap;
            uint32_t desired = bitmap & ~mask;
            
            if (atomic_compare_exchange_weak_explicit(
                    &pool->free_bitmap[i], &expected, desired,
                    memory_order_acq_rel, memory_order_acquire)) {
                
                /* Successfully claimed */
                size_t index = i * 32 + bit;
                
                /* Update statistics */
                size_t allocated = atomic_fetch_add_explicit(
                    &pool->allocated, 1, memory_order_relaxed) + 1;
                
                size_t peak = atomic_load_explicit(&pool->peak_usage, 
                                                   memory_order_relaxed);
                while (allocated > peak) {
                    if (atomic_compare_exchange_weak_explicit(
                            &pool->peak_usage, &peak, allocated,
                            memory_order_relaxed, memory_order_relaxed))
                        break;
                }
                
                return pool->buffers[index];
            }
            
            /* CAS failed, reload and retry */
            bitmap = atomic_load_explicit(&pool->free_bitmap[i], 
                                         memory_order_acquire);
        }
    }
    
    /* Pool is full */
    return NULL;
}

void
hev_memory_pool_free(HevMemoryPool *pool, void *ptr)
{
    if (!pool || !ptr)
        return;
    
    /* Find buffer index */
    size_t index = 0;
    bool found = false;
    
    for (size_t i = 0; i < pool->buffer_count; i++) {
        if (pool->buffers[i] == ptr) {
            index = i;
            found = true;
            break;
        }
    }
    
    if (!found)
        return; /* Not from this pool */
    
    /* Set bit back to 1 (free) */
    size_t word = index / 32;
    size_t bit = index % 32;
    uint32_t mask = 1U << bit;
    
    atomic_fetch_or_explicit(&pool->free_bitmap[word], mask, 
                            memory_order_release);
    
    atomic_fetch_sub_explicit(&pool->allocated, 1, memory_order_relaxed);
}

void
hev_memory_pool_get_stats(HevMemoryPool *pool, 
                         size_t *allocated, 
                         size_t *peak)
{
    if (!pool)
        return;
    
    if (allocated)
        *allocated = atomic_load_explicit(&pool->allocated, 
                                         memory_order_relaxed);
    
    if (peak)
        *peak = atomic_load_explicit(&pool->peak_usage, 
                                    memory_order_relaxed);
}
