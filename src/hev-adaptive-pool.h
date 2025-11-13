/*
 ============================================================================
 Name        : hev-adaptive-pool.h
 Author      : Performance Optimization Team
 Copyright   : Copyright (c) 2025
 Description : Adaptive Thread Pool with Auto-Scaling
 ============================================================================
 */

#ifndef __HEV_ADAPTIVE_POOL_H__
#define __HEV_ADAPTIVE_POOL_H__

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>

typedef struct _HevAdaptivePool HevAdaptivePool;
typedef void (*HevAdaptivePoolTask)(void *data);

/* Pool configuration */
typedef struct {
    int min_threads;
    int max_threads;
    int scale_up_threshold;    /* Queue depth to trigger scale-up */
    int scale_down_threshold;  /* Idle threads to trigger scale-down */
    int adjustment_interval;   /* Seconds between adjustments */
} HevAdaptivePoolConfig;

/**
 * hev_adaptive_pool_new:
 * @config: pool configuration
 *
 * Create adaptive thread pool
 *
 * Returns: HevAdaptivePool or NULL
 *
 * Since: 2.0
 */
HevAdaptivePool *hev_adaptive_pool_new(HevAdaptivePoolConfig *config);

/**
 * hev_adaptive_pool_destroy:
 * @pool: HevAdaptivePool
 *
 * Destroy adaptive pool
 *
 * Since: 2.0
 */
void hev_adaptive_pool_destroy(HevAdaptivePool *pool);

/**
 * hev_adaptive_pool_submit:
 * @pool: HevAdaptivePool
 * @task: task function
 * @data: user data
 *
 * Submit task to pool
 *
 * Returns: 0 on success, -1 on error
 *
 * Since: 2.0
 */
int hev_adaptive_pool_submit(HevAdaptivePool *pool,
                            HevAdaptivePoolTask task,
                            void *data);

/**
 * hev_adaptive_pool_get_stats:
 * @pool: HevAdaptivePool
 * @active: (out) active threads
 * @idle: (out) idle threads
 * @queue_depth: (out) pending tasks
 *
 * Get pool statistics
 *
 * Since: 2.0
 */
void hev_adaptive_pool_get_stats(HevAdaptivePool *pool,
                                int *active,
                                int *idle,
                                int *queue_depth);

/**
 * hev_adaptive_pool_adjust:
 * @pool: HevAdaptivePool
 *
 * Manually trigger pool size adjustment
 *
 * Since: 2.0
 */
void hev_adaptive_pool_adjust(HevAdaptivePool *pool);

#endif /* __HEV_ADAPTIVE_POOL_H__ */
