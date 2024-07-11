//
// Created by qxy on 24-7-10.
//
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include "coroutines.h"

#define STACK_SIZE (128 * 1024)
#define  COROUTINE_COUNT 1024


static int co_idx = -1;
static struct coroutine loc_co[COROUTINE_COUNT] = {0};
static jmp_buf main_env;


_Noreturn static void prepare_real_call_coroutine(coroutine_func func, void *arg) {
    suspend_coroutine();
    func(arg);
    struct coroutine *co = get_current_coroutine();
    co->jmp_env = NULL;
    longjmp(main_env, 1);
}

_Noreturn static void modify_stack_pointer_and_jmp_tp_coroutine(struct coroutine *co, coroutine_func func, void *arg) {
    printf("modifying stack pointer and jump to coroutine\n co: %p, func: %p, arg: %p\n", co, func, arg);
    register void *stack_base = co->stack;
    register coroutine_func f = func;
    register void *a = arg;
    // 假装这里有16字节的空间，将栈指针和帧指针区分开来
    // 16字节而不是8字节是因为要16字节对齐，否则会出现段错误，因为 movaps 指令要求操作数是16字节对齐的
    __asm__ __volatile__(
            "movq %0, %%rbp\n\t"   // load stack base to rbp
            "sub  $0x10, %0\n\t"    // make stack 16 bytes aligned
            "movq %0, %%rsp\n\t"   // load stack pointer to rsp
            :"+r"(stack_base)
            :
            );
    prepare_real_call_coroutine(f, a);
}

static int get_idle_coroutine_id() {
    for (int i = 0; i < COROUTINE_COUNT; i++) {
        if (loc_co[i].jmp_env == NULL) {
            return i;
        }
    }
    return -1;
}

static void init_coroutine(struct coroutine *co) {
    void *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    if (stack == MAP_FAILED) {
        perror("map failed");
        abort();
    }
    co->jmp_env = NULL;
    co->stack = stack + STACK_SIZE - 128;//stack grows down, and 128 bytes red zone
    co->stack_size = STACK_SIZE;
}

static void deinit_coroutine(struct coroutine *co) {
    munmap(co->stack - co->stack_size + 128, co->stack_size);
}

void init_all_coroutine() {
    for (int i = 0; i < COROUTINE_COUNT; i++) {
        init_coroutine(&loc_co[i]);
    }
}

void deinit_all_coroutine() {
    for (int i = 0; i < COROUTINE_COUNT; i++) {
        deinit_coroutine(&loc_co[i]);
    }
}

struct coroutine *get_current_coroutine() {
    if (co_idx == -1) {
        return NULL;
    }
    return &loc_co[co_idx];
}

void suspend_coroutine() {
    jmp_buf co_env;
    if (co_idx == -1) {
        printf("current co_idx is -1\n");
        abort();
    }
    loc_co[co_idx].jmp_env = co_env;
    int my_co_idx = co_idx;
    if (setjmp(co_env)) {
        co_idx = my_co_idx;
        return;
    }
    co_idx = -1;
    longjmp(main_env, 1);
}

void resume_coroutine(struct coroutine *co) {
    if (co == NULL || co->jmp_env == NULL) {
        printf("co is null or co->jmp_env is null, cannot resume\n");
        return;
    }
    if (setjmp(main_env)) {
        return;
    }
    longjmp(co->jmp_env, 1);
}

struct coroutine *new_coroutine(coroutine_func func, void *arg) {
    int co_id = get_idle_coroutine_id();
    if (co_id == -1) {
        return NULL;
    }
    co_idx = co_id;
    struct coroutine *co = &loc_co[co_idx];
    if (setjmp(main_env)) {
        return co;
    }
    modify_stack_pointer_and_jmp_tp_coroutine(co, func, arg);
}