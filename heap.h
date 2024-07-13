//
// Created by manni on 2024/7/13.
//

#ifndef EPOLL_COROUTINE_HEAP_H
#define EPOLL_COROUTINE_HEAP_H

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>

typedef struct quad_heap_node {
    int64_t key;
    void *data;
} quad_heap_node;

typedef struct quad_heap {
    int64_t capacity;
    int64_t size;
    quad_heap_node *nodes;
} quad_heap;

void min_heapify(/*uninitialized*/quad_heap *heap);

int init_heap(quad_heap *heap, int64_t capacity);
void deinit_heap(quad_heap *heap);
bool heap_empty(quad_heap *heap);

quad_heap_node heap_pop(quad_heap *heap);
quad_heap_node heap_top(quad_heap *heap);

void heap_push(quad_heap *heap, quad_heap_node node);

void print_heap(quad_heap *heap);

#endif //EPOLL_COROUTINE_HEAP_H
