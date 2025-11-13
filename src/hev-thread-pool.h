/*
 ============================================================================
 Name        : hev-thread-pool.h
 Author      : Enhanced Multi-threading Support
 Description : Thread Pool Management
 ============================================================================
 */

#ifndef __HEV_THREAD_POOL_H__
#define __HEV_THREAD_POOL_H__

#include <stddef.h>

typedef struct _HevThreadPool HevThreadPool;
typedef void (*HevThreadPoolTask) (void *data);

/**
 * hev_thread_pool_new:
 * @num_threads: number of worker threads (0 = auto-detect)
 *
 * Create a new thread pool. If num_threads is 0, automatically
 * detects the optimal number based on CPU cores.
 *
 * Returns: new thread pool instance
 */
HevThreadPool *hev_thread_pool_new (int num_threads);

/**
 * hev_thread_pool_destroy:
 * @pool: thread pool instance
 *
 * Destroy the thread pool and wait for all threads to finish.
 */
void hev_thread_pool_destroy (HevThreadPool *pool);

/**
 * hev_thread_pool_submit:
 * @pool: thread pool instance
 * @task: task function to execute
 * @data: data to pass to task
 *
 * Submit a task to the thread pool for execution.
 *
 * Returns: 0 on success, -1 on failure
 */
int hev_thread_pool_submit (HevThreadPool *pool, HevThreadPoolTask task,
                            void *data);

/**
 * hev_thread_pool_get_thread_count:
 * @pool: thread pool instance
 *
 * Get the number of worker threads.
 *
 * Returns: number of threads
 */
int hev_thread_pool_get_thread_count (HevThreadPool *pool);

/**
 * hev_thread_pool_wait_all:
 * @pool: thread pool instance
 *
 * Wait for all pending tasks to complete.
 */
void hev_thread_pool_wait_all (HevThreadPool *pool);

#endif /* __HEV_THREAD_POOL_H__ */
