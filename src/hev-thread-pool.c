/*
 ============================================================================
 Name        : hev-thread-pool.c
 Author      : Enhanced Multi-threading Support
 Description : Thread Pool Management Implementation
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/sysinfo.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#include "hev-thread-pool.h"
#include "hev-logger.h"

#define MAX_QUEUE_SIZE 10000
#define MIN_THREADS 2
#define MAX_THREADS 64

typedef struct _HevWorkItem HevWorkItem;

struct _HevWorkItem
{
    HevThreadPoolTask task;
    void *data;
    HevWorkItem *next;
};

struct _HevThreadPool
{
    pthread_t *threads;
    int num_threads;
    int shutdown;

    HevWorkItem *queue_head;
    HevWorkItem *queue_tail;
    int queue_size;
    int active_threads;

    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    pthread_cond_t done_cond;
};

static int
get_cpu_count (void)
{
    int cpu_count = 1;

#ifdef __linux__
    cpu_count = get_nprocs ();
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
    size_t len = sizeof (cpu_count);
    int mib[2] = { CTL_HW, HW_NCPU };
    if (sysctl (mib, 2, &cpu_count, &len, NULL, 0) != 0)
        cpu_count = 1;
#elif defined(_WIN32)
    SYSTEM_INFO sysinfo;
    GetSystemInfo (&sysinfo);
    cpu_count = sysinfo.dwNumberOfProcessors;
#else
    /* Fallback */
    cpu_count = sysconf (_SC_NPROCESSORS_ONLN);
    if (cpu_count < 1)
        cpu_count = 1;
#endif

    /* Clamp to reasonable values */
    if (cpu_count < MIN_THREADS)
        cpu_count = MIN_THREADS;
    if (cpu_count > MAX_THREADS)
        cpu_count = MAX_THREADS;

    return cpu_count;
}

static void *
worker_thread (void *arg)
{
    HevThreadPool *pool = (HevThreadPool *)arg;

    LOG_D ("thread pool worker started");

    while (1) {
        HevWorkItem *item;

        pthread_mutex_lock (&pool->queue_mutex);

        /* Wait for work or shutdown */
        while (pool->queue_size == 0 && !pool->shutdown) {
            pthread_cond_wait (&pool->queue_cond, &pool->queue_mutex);
        }

        if (pool->shutdown && pool->queue_size == 0) {
            pthread_mutex_unlock (&pool->queue_mutex);
            break;
        }

        /* Get work item */
        item = pool->queue_head;
        if (item) {
            pool->queue_head = item->next;
            if (pool->queue_tail == item)
                pool->queue_tail = NULL;
            pool->queue_size--;
            pool->active_threads++;
        }

        pthread_mutex_unlock (&pool->queue_mutex);

        if (item) {
            /* Execute task */
            item->task (item->data);
            free (item);

            /* Signal completion */
            pthread_mutex_lock (&pool->queue_mutex);
            pool->active_threads--;
            if (pool->active_threads == 0 && pool->queue_size == 0)
                pthread_cond_signal (&pool->done_cond);
            pthread_mutex_unlock (&pool->queue_mutex);
        }
    }

    LOG_D ("thread pool worker stopped");
    return NULL;
}

HevThreadPool *
hev_thread_pool_new (int num_threads)
{
    HevThreadPool *pool;
    int i;

    pool = (HevThreadPool *)calloc (1, sizeof (HevThreadPool));
    if (!pool)
        return NULL;

    /* Auto-detect optimal thread count */
    if (num_threads <= 0) {
        num_threads = get_cpu_count ();
        /* Use more threads for I/O bound workload */
        num_threads = num_threads * 2;
        if (num_threads > MAX_THREADS)
            num_threads = MAX_THREADS;
    }

    pool->num_threads = num_threads;
    pool->threads = (pthread_t *)calloc (num_threads, sizeof (pthread_t));
    if (!pool->threads) {
        free (pool);
        return NULL;
    }

    pthread_mutex_init (&pool->queue_mutex, NULL);
    pthread_cond_init (&pool->queue_cond, NULL);
    pthread_cond_init (&pool->done_cond, NULL);

    LOG_I ("creating thread pool with %d workers (CPU cores: %d)",
           num_threads, get_cpu_count ());

    /* Create worker threads */
    for (i = 0; i < num_threads; i++) {
        if (pthread_create (&pool->threads[i], NULL, worker_thread, pool) !=
            0) {
            LOG_E ("failed to create worker thread %d", i);
            pool->num_threads = i;
            hev_thread_pool_destroy (pool);
            return NULL;
        }
    }

    return pool;
}

void
hev_thread_pool_destroy (HevThreadPool *pool)
{
    int i;

    if (!pool)
        return;

    LOG_D ("destroying thread pool");

    /* Signal shutdown */
    pthread_mutex_lock (&pool->queue_mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast (&pool->queue_cond);
    pthread_mutex_unlock (&pool->queue_mutex);

    /* Wait for all threads */
    for (i = 0; i < pool->num_threads; i++) {
        pthread_join (pool->threads[i], NULL);
    }

    /* Clean up queue */
    while (pool->queue_head) {
        HevWorkItem *item = pool->queue_head;
        pool->queue_head = item->next;
        free (item);
    }

    pthread_mutex_destroy (&pool->queue_mutex);
    pthread_cond_destroy (&pool->queue_cond);
    pthread_cond_destroy (&pool->done_cond);

    free (pool->threads);
    free (pool);

    LOG_I ("thread pool destroyed");
}

int
hev_thread_pool_submit (HevThreadPool *pool, HevThreadPoolTask task,
                        void *data)
{
    HevWorkItem *item;

    if (!pool || !task)
        return -1;

    item = (HevWorkItem *)malloc (sizeof (HevWorkItem));
    if (!item)
        return -1;

    item->task = task;
    item->data = data;
    item->next = NULL;

    pthread_mutex_lock (&pool->queue_mutex);

    /* Check queue size limit */
    if (pool->queue_size >= MAX_QUEUE_SIZE) {
        pthread_mutex_unlock (&pool->queue_mutex);
        free (item);
        LOG_W ("thread pool queue full");
        return -1;
    }

    /* Add to queue */
    if (pool->queue_tail) {
        pool->queue_tail->next = item;
        pool->queue_tail = item;
    } else {
        pool->queue_head = item;
        pool->queue_tail = item;
    }
    pool->queue_size++;

    /* Wake up a worker */
    pthread_cond_signal (&pool->queue_cond);
    pthread_mutex_unlock (&pool->queue_mutex);

    return 0;
}

int
hev_thread_pool_get_thread_count (HevThreadPool *pool)
{
    return pool ? pool->num_threads : 0;
}

void
hev_thread_pool_wait_all (HevThreadPool *pool)
{
    if (!pool)
        return;

    pthread_mutex_lock (&pool->queue_mutex);
    while (pool->queue_size > 0 || pool->active_threads > 0) {
        pthread_cond_wait (&pool->done_cond, &pool->queue_mutex);
    }
    pthread_mutex_unlock (&pool->queue_mutex);
}
