//
// Created by qxy on 24-7-10.
//
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <setjmp.h>
#include <time.h>
#include <string.h>
#include "coroutines.h"
#include "heap.h"
#include "queue.h"

#define STACK_SIZE (128 * 1024)
#define NAME_LEN 32

static struct array_queue co_idle_queue = {0};
static struct array_queue co_all_queue = {0};
static struct quad_heap g_timer_heap = {0};
static struct co_event_loop g_event_loop = {0};

struct coroutine {
    void *jmp_env;
    void *stack;
    ssize_t stack_size;
    char name[NAME_LEN];
    enum coroutine_status status;
};

struct co_future co_new_future() {
    struct co_future future = {
            .co = g_event_loop.current_co,
            .ready = false,
    };
    return future;
}

static enum co_error g_error = CO_SUCCESS;

__attribute__((unused)) _Noreturn
void coroutine_executor(coroutine_func func, void *arg, void *env) {
    struct coroutine *co = g_event_loop.current_co;
    jmp_buf my_env;
    if (setjmp(my_env) == 0) {
        struct co_future current_future = co_new_future();
        push_queue(g_event_loop.ready_queue, &current_future);
        co->jmp_env = my_env;
        longjmp(env, 1);
    }
    func(arg);
    co->status = COROUTINE_STATUS_IDLE;
    push_queue(&co_idle_queue, co);
    struct co_future *dst_future = pop_queue(g_event_loop.ready_queue);
    if (dst_future == NULL) {
        printf("%s: no coroutine to run\n", __func__);
        co_print_all_coroutine();
        abort();
    }
    longjmp(dst_future->co->jmp_env, 1);
}

asm(".text\n"
    "prepare_stack_switch:\n"
    "movq %rsp, -0x8(%rcx)\n"
    "movq %rcx, %rsp\n"
    "subq $0x10, %rsp\n"
    "movq %rbp, (%rsp)\n"
    "movq %rsp, %rbp\n"
    "call coroutine_executor\n");

_Noreturn void prepare_stack_switch(void *func_ptr, void *arg, void *env, void *stack_ptr);


static enum co_error init_coroutine(struct coroutine *co) {
    co->jmp_env = NULL;
    co->stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (co->stack == MAP_FAILED) {
        free(co->jmp_env);
        return CO_ALLOC_ERR;
    }
    co->stack_size = STACK_SIZE;
    co->name[0] = '\0';
    co->status = COROUTINE_STATUS_IDLE;
    return CO_SUCCESS;
}

static void deinit_coroutine(struct coroutine *co) {
    munmap(co->stack, STACK_SIZE);
    co->stack = NULL;
    co->stack_size = 0;
    co->status = COROUTINE_STATUS_IDLE;
}

static void co_switch_context(struct co_event_loop *loop, struct co_future *dst_future) {
    struct coroutine *current_co = loop->current_co;
    jmp_buf env;
    current_co->jmp_env = env;
    if (setjmp(env)) {
        loop->current_co = current_co;
        current_co->status = COROUTINE_STATUS_RUNNING;
        return;
    }
    loop->current_co = dst_future->co;
    if (dst_future->co->jmp_env == NULL) {
        printf("jmp_env is null, name = %s\n", dst_future->co->name);
        abort();
    }
    longjmp(dst_future->co->jmp_env, 1);
}

void co_wakeup(struct co_event_loop *loop, struct co_future *future) {
    if (loop == NULL || future == NULL) {
        return;
    }
    if (future->ready) {
        printf("future is already ready\n");
        abort();
    }
    future->ready = true;
    struct coroutine *co = future->co;
    co->status = COROUTINE_STATUS_READY;
    push_queue(loop->ready_queue, future);
}

static void proc_timer_event(struct co_event_loop *loop) {
    struct timespec now_spec;
    clock_gettime(CLOCK_MONOTONIC, &now_spec);
    int64_t now = now_spec.tv_sec * 1000000000 + now_spec.tv_nsec;
    while (!heap_empty(&g_timer_heap) && g_timer_heap.nodes[0].key <= now) {
        struct co_future *future = heap_pop(&g_timer_heap).data;
        co_wakeup(loop, future);
    }
}

void co_yield() {
    struct co_event_loop *loop = &g_event_loop;
    struct coroutine *co = loop->current_co;
    proc_timer_event(loop);
    struct co_future *dst_future = pop_queue(loop->ready_queue);
    if (dst_future == NULL) {
        printf("%s: no coroutine to run\n", __func__);
        return;
    }
    co->status = COROUTINE_STATUS_READY;
    struct co_future current_future = co_new_future();
    push_queue(loop->ready_queue, &current_future);
    co_switch_context(loop, dst_future);
}

void co_block() {
    struct co_event_loop *loop = &g_event_loop;
    struct coroutine *co = loop->current_co;
    proc_timer_event(loop);
    struct co_future *dst_future = pop_queue(loop->ready_queue);
    if (dst_future == NULL) {
        printf("%s: no coroutine to run\n", __func__);
        abort();
    }
    co->status = COROUTINE_STATUS_BLOCKED;
    co_switch_context(loop, dst_future);
}

void co_sleep(int64_t ns) {
    struct co_future future = co_new_future();
    struct timespec now_spec;
    clock_gettime(CLOCK_MONOTONIC, &now_spec);
    int64_t now = now_spec.tv_sec * 1000000000 + now_spec.tv_nsec;
    int64_t future_time = now + ns;
    heap_push(&g_timer_heap, (quad_heap_node) {future_time, &future});
    struct co_event_loop *loop = &g_event_loop;
    co_block();
}

static struct coroutine *get_idle_coroutine() {
    struct coroutine *co = pop_queue(&co_idle_queue);
    if (co != NULL) {
        return co;
    }
    if (queue_full(&co_all_queue)) {
        g_error = CO_QUEUE_FULL;
        return NULL;
    }
    co = malloc(sizeof(struct coroutine));
    if (co == NULL) {
        g_error = CO_ALLOC_ERR;
        return NULL;
    }
    enum co_error ret = init_coroutine(co);
    if (ret != 0) {
        g_error = ret;
        free(co);
        return NULL;
    }
    push_queue(&co_all_queue, co);
    return co;
}

enum co_error co_spawn(struct co_event_loop *loop, coroutine_func func, void *arg, char *name) {
    struct coroutine *co = get_idle_coroutine();
    if (co == NULL) {
        enum co_error ret = g_error;
        g_error = CO_SUCCESS;
        return ret;
    }
    enum co_error ret = init_coroutine(co);
    if (ret != 0) {
        free(co);
        return ret;
    }
    strncpy(co->name, name, NAME_LEN);
    jmp_buf env;
    struct coroutine *cur_co = loop->current_co;
    if (setjmp(env) == 0) {
        loop->current_co = co;
        prepare_stack_switch(func, arg, env, co->stack + co->stack_size);
    }
    loop->current_co = cur_co;
    return CO_SUCCESS;
}

int co_dispatch(struct co_event_loop *loop) {
    proc_timer_event(loop);
    while (queue_size(loop->ready_queue) > 0) {
        co_yield();
    }
    return 0;
}

struct co_event_loop *co_get_loop() {
    return &g_event_loop;
}

int64_t co_min_wait_time() {
    if (heap_empty(&g_timer_heap)) {
        return -1;
    }
    struct timespec now_spec;
    clock_gettime(CLOCK_MONOTONIC, &now_spec);
    int64_t now = now_spec.tv_sec * 1000000000 + now_spec.tv_nsec;
    if (g_timer_heap.nodes[0].key - now < 0) {
        return 0;
    }
    return g_timer_heap.nodes[0].key - now;
}

int co_setup(int max_size) {
    if (max_size <= 0) {
        return -1;
    }
    if (g_event_loop.ready_queue != NULL) {
        return -1;
    }
    if (init_queue(&co_idle_queue, max_size) != 0) {
        goto end3;
    }
    if (init_queue(&co_all_queue, max_size) != 0) {
        goto end2;
    }
    if (init_heap(&g_timer_heap, max_size) != 0) {
        goto end1;
    }
    g_event_loop.ready_queue = malloc(sizeof(struct array_queue));
    if (g_event_loop.ready_queue == NULL) {
        goto end1;
    }
    if (init_queue(g_event_loop.ready_queue, max_size) != 0) {
        goto end0;
    }
    struct coroutine *co = calloc(sizeof(struct coroutine), 1);
    if (co == NULL) {
        goto end0;
    }
    strncpy(co->name, "main", NAME_LEN);
    co->status = COROUTINE_STATUS_RUNNING;
    push_queue(&co_all_queue, co);
    g_event_loop.current_co = co;
    return 0;
    end0:
    deinit_queue(g_event_loop.ready_queue);
    end1:
    deinit_queue(&co_idle_queue);
    end2:
    deinit_queue(&co_all_queue);
    end3:
    return -1;
}

int co_teardown() {
    while (queue_size(&co_all_queue) > 0) {
        struct coroutine *co = pop_queue(&co_all_queue);
        deinit_coroutine(co);
        free(co);
    }
    deinit_queue(&co_idle_queue);
    deinit_queue(&co_all_queue);
    deinit_heap(&g_timer_heap);
    deinit_queue(g_event_loop.ready_queue);
    free(g_event_loop.ready_queue);
    g_event_loop.ready_queue = NULL;
    return 0;
}


static const char *get_status_str(enum coroutine_status status) {
    static const char *status_str[] = {
            "IDLE",
            "RUNNING",
            "READY",
            "BLOCKED",
            "SLEEPING",
    };
    if (status >= sizeof(status_str) / sizeof(status_str[0]) != 0) {
        return "UNKNOWN";
    }

    return status_str[status];
}

void co_print_all_coroutine() {
    printf("Ready future:     %d\n", queue_size(g_event_loop.ready_queue));
    printf("Idle coroutine:   %d\n", queue_size(&co_idle_queue));
    printf("All coroutine:    %d\n", queue_size(&co_all_queue));
    for (int i = 0; i < queue_size(&co_all_queue); i++) {
        struct coroutine *co = co_all_queue.coroutines[queue_cvt_pos(&co_all_queue, i)];
        if (co->status == COROUTINE_STATUS_IDLE) {
            continue;
        }
        printf("    coroutine %s: %s\n", co->name, get_status_str(co->status));
    }
    printf("===END===\n");
}