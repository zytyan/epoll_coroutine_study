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
    COROUTINE_STATUS_PENDING,
    COROUTINE_STATUS_BLOCKED,
};
struct coroutine {
    void *jmp_env;
    void *stack;
    ssize_t stack_size;
    char *name;
    enum coroutine_status status;
};

typedef void (*coroutine_func)(void *);

struct coroutine *setup_coroutine(uint32_t max_count);

int teardown_coroutine();

struct coroutine *get_current_coroutine();

int new_coroutine(coroutine_func func, void *arg);

void co_pending();

void co_block();

struct coroutine *co_get_pending();

void co_clear_block(struct coroutine *co);

void print_all_coroutine();

bool co_has_pending();

static inline bool is_coroutine_runnable(struct coroutine *co) {
    if (co == NULL) {
        return false;
    }
    return co->status == COROUTINE_STATUS_PENDING && co->jmp_env != NULL;
}

#endif //EPOLL_COROUTINE_COROUTINES_H
