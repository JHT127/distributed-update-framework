#include "thread_pool.h"
#include <stdlib.h>
#include <stdio.h>

/* Thread-local storage: each worker thread stores its own slot index here.
   pthread_getspecific/setspecific gives every thread its own private copy. */
static pthread_key_t  slot_key;
static pthread_once_t slot_key_once = PTHREAD_ONCE_INIT;

static void make_slot_key(void) {
    pthread_key_create(&slot_key, free);
}

/* Worker context: pool pointer + this worker's slot index */
typedef struct {
    ThreadPool *pool;
    int         slot;
} WorkerCtx;

static void *worker_loop(void *arg) {
    WorkerCtx *ctx = (WorkerCtx *)arg;
    ThreadPool *pool = ctx->pool;

    /* store slot index in thread-local storage so handle_client() can read it */
    pthread_once(&slot_key_once, make_slot_key);
    int *slot_ptr = malloc(sizeof(int));
    *slot_ptr = ctx->slot;
    pthread_setspecific(slot_key, slot_ptr);
    free(ctx);   /* ctx was malloc'd in thread_pool_create */

    while (1) {
        pthread_mutex_lock(&pool->mutex);

        while (pool->count == 0 && !pool->shutdown)
            pthread_cond_wait(&pool->not_empty, &pool->mutex);

        if (pool->shutdown && pool->count == 0) {
            pthread_mutex_unlock(&pool->mutex);
            return NULL;
        }

        Task t = pool->queue[pool->head];
        pool->head = (pool->head + 1) % pool->queue_size;
        pool->count--;

        pthread_cond_signal(&pool->not_full);
        pthread_mutex_unlock(&pool->mutex);

        t.fn(t.arg);
    }
}

ThreadPool *thread_pool_create(int pool_size, int queue_size) {
    pthread_once(&slot_key_once, make_slot_key);

    ThreadPool *pool = malloc(sizeof(ThreadPool));
    pool->threads    = malloc(sizeof(pthread_t) * pool_size);
    pool->queue      = malloc(sizeof(Task) * queue_size);
    pool->queue_size = queue_size;
    pool->pool_size  = pool_size;
    pool->head = pool->tail = pool->count = 0;
    pool->shutdown = 0;

    pthread_mutex_init(&pool->mutex,    NULL);
    pthread_cond_init(&pool->not_empty, NULL);
    pthread_cond_init(&pool->not_full,  NULL);

    for (int i = 0; i < pool_size; i++) {
        WorkerCtx *ctx = malloc(sizeof(WorkerCtx));
        ctx->pool = pool;
        ctx->slot = i;   /* slot 0, 1, 2 … pool_size-1 */
        pthread_create(&pool->threads[i], NULL, worker_loop, ctx);
    }

    return pool;
}

void thread_pool_submit(ThreadPool *pool, task_fn fn, void *arg) {
    pthread_mutex_lock(&pool->mutex);

    while (pool->count == pool->queue_size && !pool->shutdown)
        pthread_cond_wait(&pool->not_full, &pool->mutex);

    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->mutex);
        return;
    }

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

/* Called from handle_client() — returns this worker's slot (0..pool_size-1) */
int thread_pool_get_slot(void) {
    int *slot_ptr = (int *)pthread_getspecific(slot_key);
    if (!slot_ptr) return 0;   /* fallback — should never happen */
    return *slot_ptr;
}