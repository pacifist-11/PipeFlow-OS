#define _POSIX_C_SOURCE 200809L
#include "record.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <ctype.h>

#define QUEUE_CAPACITY 64

typedef struct {
    char data[4096];
    bool is_eof;
} task_item_t;

typedef struct {
    task_item_t items[QUEUE_CAPACITY];
    int head;
    int tail;
    int count;
    bool closed;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} queue_t;

static queue_t g_queue;
static pthread_mutex_t g_stdout_lock = PTHREAD_MUTEX_INITIALIZER;

static void queue_init(queue_t *q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->closed = false;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

static void queue_push(queue_t *q, const char *data, bool is_eof) {
    pthread_mutex_lock(&q->lock);
    while (q->count == QUEUE_CAPACITY && !q->closed) {
        pthread_cond_wait(&q->not_full, &q->lock);
    }
    task_item_t *item = &q->items[q->tail];
    item->is_eof = is_eof;
    if (data) {
        strncpy(item->data, data, sizeof(item->data) - 1);
        item->data[sizeof(item->data) - 1] = '\0';
    } else {
        item->data[0] = '\0';
    }
    q->tail = (q->tail + 1) % QUEUE_CAPACITY;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

static bool queue_pop(queue_t *q, task_item_t *out) {
    pthread_mutex_lock(&q->lock);
    while (q->count == 0 && !q->closed) {
        pthread_cond_wait(&q->not_empty, &q->lock);
    }
    if (q->count == 0 && q->closed) {
        pthread_mutex_unlock(&q->lock);
        return false;
    }
    *out = q->items[q->head];
    q->head = (q->head + 1) % QUEUE_CAPACITY;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    return true;
}

static void *worker_func(void *arg) {
    (void)arg;
    task_item_t task;
    while (queue_pop(&g_queue, &task)) {
        if (task.is_eof) break;

        // Perform compute work: ROT13 + Tagging
        size_t len = strlen(task.data);
        for (size_t i = 0; i < len; i++) {
            char c = task.data[i];
            if (isalpha((unsigned char)c)) {
                char base = islower((unsigned char)c) ? 'a' : 'A';
                task.data[i] = (char)(base + (c - base + 13) % 26);
            }
        }

        // Thread-safe output write to downstream pipe
        pthread_mutex_lock(&g_stdout_lock);
        pf_rec_status_t st = record_write_line(STDOUT_FILENO, task.data);
        pthread_mutex_unlock(&g_stdout_lock);

        if (st == PF_REC_EPIPE) break;
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, NULL);

    int num_threads = 4;
    if (argc > 1) {
        num_threads = atoi(argv[1]);
        if (num_threads < 1) num_threads = 1;
        if (num_threads > 16) num_threads = 16;
    }

    fprintf(stderr, "[pf_parallel_worker] Starting %d worker threads (bounded queue: %d)...\n",
            num_threads, QUEUE_CAPACITY);

    queue_init(&g_queue);

    pthread_t threads[16];
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, worker_func, NULL);
    }

    char buffer[4096];
    while (1) {
        pf_rec_status_t st = record_read_line(STDIN_FILENO, buffer, sizeof(buffer));
        if (st == PF_REC_EOF) break;
        if (st != PF_REC_OK) break;
        queue_push(&g_queue, buffer, false);
    }

    // Signal EOF to all worker threads
    pthread_mutex_lock(&g_queue.lock);
    g_queue.closed = true;
    pthread_cond_broadcast(&g_queue.not_empty);
    pthread_mutex_unlock(&g_queue.lock);

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    fprintf(stderr, "[pf_parallel_worker] All worker threads joined cleanly. EOF.\n");
    return 0;
}
