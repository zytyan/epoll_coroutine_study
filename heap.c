#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include "heap.h"

#define K 4U  // 定义四叉堆的度数


static void swap(quad_heap_node *a, quad_heap_node *b) {
    quad_heap_node temp = *a;
    *a = *b;
    *b = temp;
}

// 创建四叉堆
quad_heap *create_quad_heap(int capacity) {
    quad_heap *heap = (quad_heap *) malloc(sizeof(quad_heap));
    heap->capacity = capacity;
    heap->size = 0;
    heap->nodes = (quad_heap_node *) malloc(sizeof(quad_heap_node) * capacity);
    return heap;
}

// 扩展四叉堆的容量
static int expand_quad_heap(quad_heap *heap) {
    if (heap->capacity >= UINT32_MAX / 2) {
        // 如果容量可能溢出，则不再扩展
        return -1;
    }
    int64_t new_cap = heap->capacity * 2;
    quad_heap_node *new_nodes = (quad_heap_node *) realloc(heap->nodes, sizeof(quad_heap_node) * heap->capacity);
    if (new_nodes == NULL) {
        return -1;
    }
    heap->nodes = new_nodes;
    heap->capacity = new_cap;
    return 0;
}

// 获取父节点的索引
static int64_t parent(int64_t i) {
    return (i - 1) / K;
}

// 获取第k个子节点的索引
static int64_t child(int64_t i, int64_t k) {
    return K * i + k;
}

static int64_t min_non_leaf_idx(quad_heap *heap) {
    if (heap->size == 0 || heap->size == 1) {
        return -1;
    }
    return (heap->size - 1) / K;
}

static void heap_up(quad_heap *heap, int64_t i) {
    while (i != 0 && heap->nodes[parent(i)].key > heap->nodes[i].key) {
        swap(&heap->nodes[i], &heap->nodes[parent(i)]);
        i = parent(i);
    }
}


static void heap_down(quad_heap *heap, int64_t i) {
    while (1) {
        int64_t min_index = i;
        for (int64_t k = 1; k <= K; k++) {
            int64_t child_index = child(i, k);
            if (child_index < heap->size && heap->nodes[child_index].key < heap->nodes[min_index].key) {
                min_index = child_index;
            }
        }
        if (min_index == i) {
            break;
        }
        swap(&heap->nodes[i], &heap->nodes[min_index]);
        i = min_index;
    }
}

// 最小堆化
void min_heapify(quad_heap *heap/*uninitialized*/) {
    for (int64_t idx = min_non_leaf_idx(heap); idx >= 0; idx--) {
        heap_down(heap, idx);
    }
}

bool heap_empty(quad_heap *heap) {
    return heap->size == 0;
}

int init_heap(quad_heap *heap, int64_t capacity) {
    heap->capacity = capacity;
    heap->size = 0;
    heap->nodes = (quad_heap_node *) malloc(sizeof(quad_heap_node) * capacity);
    if (heap->nodes == NULL) {
        return -1;
    }
    return 0;
}

void deinit_heap(quad_heap *heap) {
    free(heap->nodes);
    heap->nodes = NULL;
    heap->size = 0;
    heap->capacity = 0;
}

// 删除最小元素
quad_heap_node heap_pop(quad_heap *heap) {
    if (heap->size <= 0) {
        return (quad_heap_node) {INT64_MIN, NULL};
    }

    if (heap->size == 1) {
        heap->size--;
        return heap->nodes[0];
    }

    quad_heap_node root = heap->nodes[0];
    heap->nodes[0] = heap->nodes[heap->size - 1];
    heap->size--;
    heap_down(heap, 0);
    return root;
}

quad_heap_node heap_top(quad_heap *heap) {
    if (heap->size <= 0) {
        return (quad_heap_node) {INT64_MIN, NULL};
    }
    return heap->nodes[0];
}

// 插入元素
void heap_push(quad_heap *heap, quad_heap_node data) {
    if (heap->size == heap->capacity) {
        expand_quad_heap(heap);
    }
    heap->nodes[heap->size] = data;
    heap->size++;
    heap_up(heap, heap->size - 1);
}


// 打印堆
void print_heap(quad_heap *heap) {
    for (int i = 0; i < heap->size; i++) {
        printf("%ld ", heap->nodes[i].key);
    }
    printf("\n");
}

