//
// Created by manni on 2024/7/16.
//

#ifndef EPOLL_COROUTINE_QUEUE_H
#define EPOLL_COROUTINE_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

struct array_queue {
    void **coroutines;
    uint32_t cap;
    uint32_t head;
    uint32_t tail;
};


int init_queue(struct array_queue *queue, uint32_t cap);

void deinit_queue(struct array_queue *queue);

void push_queue(struct array_queue *queue, void *co);

void *pop_queue(struct array_queue *queue);

uint32_t queue_size(struct array_queue *queue);

uint32_t queue_cvt_pos(struct array_queue *queue, uint32_t index);

bool queue_full(struct array_queue *queue);

#endif //EPOLL_COROUTINE_QUEUE_H
