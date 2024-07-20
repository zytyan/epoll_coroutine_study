//
// Created by qxy on 24-7-10.
//

#ifndef EPOLL_COROUTINE_COROUTINES_H
#define EPOLL_COROUTINE_COROUTINES_H


#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

enum coroutine_status {
    COROUTINE_STATUS_IDLE,
    COROUTINE_STATUS_RUNNING,
    COROUTINE_STATUS_READY,
    COROUTINE_STATUS_BLOCKED,
    COROUTINE_STATUS_SLEEPING,
};

struct co_future {
    struct coroutine *co;
    bool ready;
};
struct co_event_loop {
    void *ready_queue;
    void *current_co;
    int64_t sleep_count;
};

typedef void (*coroutine_func)(void *);

void co_wakeup(struct co_event_loop *loop, struct co_future *future);

void co_block();

void co_yield();

void co_sleep(int64_t ns);

int co_dispatch(struct co_event_loop *loop);

int co_spawn(struct co_event_loop *loop, coroutine_func func, void *arg, char *name);

struct co_future co_new_future();

struct co_event_loop *co_get_loop();

int64_t co_min_wait_time();

int co_setup(int max_size);

int co_teardown();

void co_print_all_coroutine();

#endif //EPOLL_COROUTINE_COROUTINES_H
