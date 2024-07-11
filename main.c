//
// Created by qxy on 24-7-10.
//
#include <stdio.h>
#include <setjmp.h>
#include "coroutines.h"

void test_coroutine_1(__attribute__((unused)) void *dummy) {
    printf("this is test coroutine 1 before, current coroutine: %p\n", get_current_coroutine());
    suspend_coroutine();
    printf("this is test coroutine 1 after, current coroutine: %p\n", get_current_coroutine());
}

void test_coroutine_2(__attribute__((unused)) void *dummy) {
    printf("this is test coroutine 2 before, current coroutine: %p\n", get_current_coroutine());
    suspend_coroutine();
    printf("this is test coroutine 2 after, current coroutine: %p\n", get_current_coroutine());
}

void test_coroutine_3(__attribute__((unused)) void *dummy) {
    for (int i = 0; i < 30; i++) {
        printf("this is test coroutine 3 before i=%d, current coroutine: %p\n", i, get_current_coroutine());
        suspend_coroutine();
        printf("this is test coroutine 3 after i=%d, current coroutine: %p\n", i, get_current_coroutine());
    }
}

void test_coroutine_4(__attribute__((unused)) void *dummy) {
    for (int i = 0; i < 30; i++) {
        printf("this is test coroutine 4 before i=%d, current coroutine: %p\n", i, get_current_coroutine());
        suspend_coroutine();
        printf("this is test coroutine 4 after i=%d, current coroutine: %p\n", i, get_current_coroutine());
    }
}

int main() {
    init_all_coroutine();
    printf("sizeof(jmp_buf) = %ld", sizeof(jmp_buf));
    struct coroutine *co1 = new_coroutine(test_coroutine_1, NULL);
    struct coroutine *co2 = new_coroutine(test_coroutine_2, NULL);
    resume_coroutine(co1);
    resume_coroutine(co2);
    resume_coroutine(co1);
    resume_coroutine(co2);
    resume_coroutine(co1);

    struct coroutine *co3 = new_coroutine(test_coroutine_3, NULL);
    struct coroutine *co4 = new_coroutine(test_coroutine_4, NULL);
    while (is_coroutine_running(co3) || is_coroutine_running(co4)) {
        resume_coroutine(co3);
        resume_coroutine(co4);
    }
    deinit_all_coroutine();
    return 0;
}