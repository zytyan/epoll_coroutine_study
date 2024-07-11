//
// Created by qxy on 24-7-10.
//

#ifndef EPOLL_COROUTINE_COROUTINES_H
#define EPOLL_COROUTINE_COROUTINES_H


#include <unistd.h>
#include <stdbool.h>

struct coroutine {
    void *jmp_env;
    void *stack;
    ssize_t stack_size;
};

typedef void (*coroutine_func)(void *);

void init_all_coroutine();

void deinit_all_coroutine();

struct coroutine *get_current_coroutine();

void suspend_coroutine();

void resume_coroutine(struct coroutine *co);

struct coroutine *new_coroutine(coroutine_func func, void *arg);

static inline bool is_coroutine_running(struct coroutine *co) {
    return co != NULL && co->jmp_env != NULL;
}

#endif //EPOLL_COROUTINE_COROUTINES_H
