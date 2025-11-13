/*
 ============================================================================
 Name        : hev-adaptive-pool.c
 Author      : Performance Optimization Team
 Copyright   : Copyright (c) 2025
 Description : Adaptive Thread Pool Implementation
 ============================================================================
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "hev-adaptive-pool.h"
#include "hev-ring-buffer.h"

typedef struct {
    HevAdaptivePoolTask task;
    void *data;
} AdaptivePoolWork;

typedef struct {
    pthread_t thread;
    _Atomic bool active;
    _Atomic bool should_exit;
    time_t last_work;
} AdaptivePoolWorker;

struct _HevAdaptivePool {
    AdaptivePoolWorker *workers;
    int min_threads;
    int max_threads;
    _Atomic int current_threads;
    _Atomic int active_threads;
    _Atomic int idle_threads;
    
    HevRingBuffer *work_queue;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    
    _Atomic bool running;
    
    int scale_up_threshold;
    int scale_down_threshold;
    int adjustment_interval;
    time_t last_adjustment;
    
    pthread_t adjuster_thread;
};

static void *
worker_thread_func(void *arg)
{
    HevAdaptivePool *pool = arg;
    
    while (atomic_load(&pool->running)) {
        AdaptivePoolWork *work = NULL;
        
        /* Try to get work from queue */
        pthread_mutex_lock(&pool->mutex);
        
        atomic_fetch_add(&pool->idle_threads, 1);
        
        while (atomic_load(&pool->running) && 
               hev_ring_buffer_is_empty(pool->work_queue)) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }
        
        atomic_fetch_sub(&pool->idle_threads, 1);
        
        if (!atomic_load(&pool->running)) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
        
        work = hev_ring_buffer_pop(pool->work_queue);
        pthread_mutex_unlock(&pool->mutex);
        
        if (work) {
            atomic_fetch_add(&pool->active_threads, 1);
            
            /* Execute task */
            work->task(work->data);
            free(work);
            
            atomic_fetch_sub(&pool->active_threads, 1);
        }
    }
    
    return NULL;
}

static void *
adjuster_thread_func(void *arg)
{
    HevAdaptivePool *pool = arg;
    
    while (atomic_load(&pool->running)) {
        sleep(pool->adjustment_interval);
        
        if (!atomic_load(&pool->running))
            break;
        
        hev_adaptive_pool_adjust(pool);
    }
    
    return NULL;
}

HevAdaptivePool *
hev_adaptive_pool_new(HevAdaptivePoolConfig *config)
{
    HevAdaptivePool *pool;
    
    if (!config)
        return NULL;
    
    pool = calloc(1, sizeof(HevAdaptivePool));
    if (!pool)
        return NULL;
    
    pool->min_threads = config->min_threads;
    pool->max_threads = config->max_threads;
    pool->scale_up_threshold = config->scale_up_threshold;
    pool->scale_down_threshold = config->scale_down_threshold;
    pool->adjustment_interval = config->adjustment_interval;
    
    atomic_init(&pool->current_threads, 0);
    atomic_init(&pool->active_threads, 0);
    atomic_init(&pool->idle_threads, 0);
    atomic_init(&pool->running, true);
    
    pool->work_queue = hev_ring_buffer_new();
    if (!pool->work_queue) {
        free(pool);
        return NULL;
    }
    
    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->cond, NULL);
    
    /* Allocate worker array */
    pool->workers = calloc(pool->max_threads, sizeof(AdaptivePoolWorker));
    if (!pool->workers) {
        hev_ring_buffer_destroy(pool->work_queue);
        free(pool);
        return NULL;
    }
    
    /* Start minimum threads */
    for (int i = 0; i < pool->min_threads; i++) {
        pthread_create(&pool->workers[i].thread, NULL, 
                      worker_thread_func, pool);
        atomic_init(&pool->workers[i].active, true);
        atomic_init(&pool->workers[i].should_exit, false);
        atomic_fetch_add(&pool->current_threads, 1);
    }
    
    /* Start adjuster thread */
    pthread_create(&pool->adjuster_thread, NULL, 
                  adjuster_thread_func, pool);
    
    return pool;
}

void
hev_adaptive_pool_destroy(HevAdaptivePool *pool)
{
    if (!pool)
        return;
    
    atomic_store(&pool->running, false);
    
    /* Wake up all workers */
    pthread_mutex_lock(&pool->mutex);
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
    
    /* Wait for adjuster */
    pthread_join(pool->adjuster_thread, NULL);
    
    /* Wait for all workers */
    int count = atomic_load(&pool->current_threads);
    for (int i = 0; i < count; i++) {
        if (atomic_load(&pool->workers[i].active))
            pthread_join(pool->workers[i].thread, NULL);
    }
    
    hev_ring_buffer_destroy(pool->work_queue);
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
    free(pool->workers);
    free(pool);
}

int
hev_adaptive_pool_submit(HevAdaptivePool *pool,
                        HevAdaptivePoolTask task,
                        void *data)
{
    if (!pool || !task)
        return -1;
    
    AdaptivePoolWork *work = malloc(sizeof(AdaptivePoolWork));
    if (!work)
        return -1;
    
    work->task = task;
    work->data = data;
    
    pthread_mutex_lock(&pool->mutex);
    
    if (hev_ring_buffer_push(pool->work_queue, work) < 0) {
        pthread_mutex_unlock(&pool->mutex);
        free(work);
        return -1;
    }
    
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
    
    return 0;
}

void
hev_adaptive_pool_get_stats(HevAdaptivePool *pool,
                           int *active,
                           int *idle,
                           int *queue_depth)
{
    if (!pool)
        return;
    
    if (active)
        *active = atomic_load(&pool->active_threads);
    
    if (idle)
        *idle = atomic_load(&pool->idle_threads);
    
    if (queue_depth)
        *queue_depth = hev_ring_buffer_size(pool->work_queue);
}

void
hev_adaptive_pool_adjust(HevAdaptivePool *pool)
{
    if (!pool)
        return;
    
    int queue_depth = hev_ring_buffer_size(pool->work_queue);
    int idle = atomic_load(&pool->idle_threads);
    int current = atomic_load(&pool->current_threads);
    
    /* Scale up if queue is growing */
    if (queue_depth > pool->scale_up_threshold && 
        idle < 2 && 
        current < pool->max_threads) {
        
        /* Add one more thread */
        pthread_create(&pool->workers[current].thread, NULL,
                      worker_thread_func, pool);
        atomic_init(&pool->workers[current].active, true);
        atomic_fetch_add(&pool->current_threads, 1);
    }
    
    /* Scale down if too many idle threads */
    else if (idle > pool->scale_down_threshold &&
             queue_depth < 10 &&
             current > pool->min_threads) {
        
        /* This is simplified - proper implementation would 
         * need to signal specific thread to exit gracefully */
        atomic_fetch_sub(&pool->current_threads, 1);
    }
}
