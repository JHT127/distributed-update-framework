#pragma once
#include <pthread.h>

typedef void (*task_fn)(void *arg);

typedef struct {
    task_fn  fn;
    void    *arg;
} Task;

typedef struct {
    pthread_t       *threads;
    Task            *queue;
    int              queue_size;
    int              head, tail, count;
    int              pool_size;
    int              shutdown;
    pthread_mutex_t  mutex;
    pthread_cond_t   not_empty;
    pthread_cond_t   not_full;
} ThreadPool;

ThreadPool *thread_pool_create(int pool_size, int queue_size);
void        thread_pool_submit(ThreadPool *pool, task_fn fn, void *arg);
void        thread_pool_destroy(ThreadPool *pool);