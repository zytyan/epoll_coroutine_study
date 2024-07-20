//
// Created by manni on 2024/7/16.
//

#include <malloc.h>
#include <stdbool.h>
#include "queue.h"


int init_queue(struct array_queue *queue, uint32_t cap) {
    queue->coroutines = calloc(sizeof(void *), cap);
    if (queue->coroutines == NULL) {
        return -1;
    }
    queue->cap = cap;
    queue->head = 0;
    queue->tail = 0;
    return 0;
}

void deinit_queue(struct array_queue *queue) {
    free(queue->coroutines);
    queue->coroutines = NULL;
    queue->cap = 0;
    queue->head = 0;
    queue->tail = 0;
}

void push_queue(struct array_queue *queue, void *co) {
    if ((queue->tail + 1) % queue->cap == queue->head) {
        return;
    }
    queue->coroutines[queue->tail] = co;
    queue->tail = (queue->tail + 1) % queue->cap;
}

void *pop_queue(struct array_queue *queue) {
    if (queue->head == queue->tail) {
        return NULL;
    }
    void *co = queue->coroutines[queue->head];
    queue->head = (queue->head + 1) % queue->cap;
    return co;
}

uint32_t queue_size(struct array_queue *queue) {
    return (queue->tail - queue->head + queue->cap) % queue->cap;
}

uint32_t queue_cvt_pos(struct array_queue *queue, uint32_t index) {
    return (queue->head + index) % queue->cap;
}

bool queue_full(struct array_queue *queue) {
    return (queue->tail + 1) % queue->cap == queue->head;
}
