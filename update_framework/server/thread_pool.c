#include "thread_pool.h"
#include <stdlib.h>
#include <stdio.h>

// each worker thread runs this loop forever until shutdown
static void *worker_loop(void *arg) {
    ThreadPool *pool = (ThreadPool *)arg;

    while (1) {
        pthread_mutex_lock(&pool->mutex);

        // sleep until there is a task or we are shutting down
        while (pool->count == 0 && !pool->shutdown)
            pthread_cond_wait(&pool->not_empty, &pool->mutex);

        if (pool->shutdown && pool->count == 0) {
            pthread_mutex_unlock(&pool->mutex);
            return NULL;
        }

        // pop task from queue
        Task t = pool->queue[pool->head];
        pool->head = (pool->head + 1) % pool->queue_size;
        pool->count--;

        pthread_cond_signal(&pool->not_full);
        pthread_mutex_unlock(&pool->mutex);

        // execute task outside the lock
        t.fn(t.arg);
    }
}

ThreadPool *thread_pool_create(int pool_size, int queue_size) {
    ThreadPool *pool = malloc(sizeof(ThreadPool));
    pool->threads    = malloc(sizeof(pthread_t) * pool_size);
    pool->queue      = malloc(sizeof(Task) * queue_size);
    pool->queue_size = queue_size;
    pool->pool_size  = pool_size;
    pool->head = pool->tail = pool->count = 0;
    pool->shutdown = 0;

    pthread_mutex_init(&pool->mutex,     NULL);
    pthread_cond_init(&pool->not_empty,  NULL);
    pthread_cond_init(&pool->not_full,   NULL);

    for (int i = 0; i < pool_size; i++)
        pthread_create(&pool->threads[i], NULL, worker_loop, pool);

    return pool;
}

void thread_pool_submit(ThreadPool *pool, task_fn fn, void *arg) {
    pthread_mutex_lock(&pool->mutex);

    // wait if queue is full
    while (pool->count == pool->queue_size && !pool->shutdown)
        pthread_cond_wait(&pool->not_full, &pool->mutex);

    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->mutex);
        return;
    }

    // push task onto queue
    pool->queue[pool->tail].fn  = fn;
    pool->queue[pool->tail].arg = arg;
    pool->tail = (pool->tail + 1) % pool->queue_size;
    pool->count++;

    pthread_cond_signal(&pool->not_empty);
    pthread_mutex_unlock(&pool->mutex);
}

void thread_pool_destroy(ThreadPool *pool) {
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->not_empty);
    pthread_mutex_unlock(&pool->mutex);

    for (int i = 0; i < pool->pool_size; i++)
        pthread_join(pool->threads[i], NULL);

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->not_empty);
    pthread_cond_destroy(&pool->not_full);
    free(pool->threads);
    free(pool->queue);
    free(pool);
}