//
// Created by qxy on 24-7-10.
//
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <setjmp.h>
#include "coroutines.h"

#define STACK_SIZE (128 * 1024)

// region coroutine_queue
struct coroutine_queue {
    struct coroutine **coroutines;
    uint32_t cap;
    uint32_t head;
    uint32_t tail;
};

static int init_queue(struct coroutine_queue *queue, uint32_t cap) {
    queue->coroutines = calloc(sizeof(struct coroutine *), cap);
    if (queue->coroutines == NULL) {
        return -1;
    }
    queue->cap = cap;
    queue->head = 0;
    queue->tail = 0;
    return 0;
}

static void deinit_queue(struct coroutine_queue *queue) {
    free(queue->coroutines);
    queue->coroutines = NULL;
    queue->cap = 0;
    queue->head = 0;
    queue->tail = 0;
}

static void push_queue(struct coroutine_queue *queue, struct coroutine *co) {
    if ((queue->tail + 1) % queue->cap == queue->head) {
        return;
    }
    queue->coroutines[queue->tail] = co;
    queue->tail = (queue->tail + 1) % queue->cap;
}

static struct coroutine *pop_queue(struct coroutine_queue *queue) {
    if (queue->head == queue->tail) {
        return NULL;
    }
    struct coroutine *co = queue->coroutines[queue->head];
    queue->head = (queue->head + 1) % queue->cap;
    return co;
}

static uint32_t queue_size(struct coroutine_queue *queue) {
    return (queue->tail - queue->head + queue->cap) % queue->cap;
}

static uint32_t queue_cvt_pos(struct coroutine_queue *queue, uint32_t index) {
    return (queue->head + index) % queue->cap;
}

static bool queue_full(struct coroutine_queue *queue) {
    return (queue->tail + 1) % queue->cap == queue->head;
}

// endregion
static struct coroutine_queue co_idle_queue = {0};
static struct coroutine_queue co_pending_queue = {0};
static struct coroutine_queue co_all_queue = {0};
static uint32_t max_coroutine_count = 0;
static uint32_t inited_coroutine_count = 0;
static struct coroutine *g_main_co = NULL;

static void init_coroutine(struct coroutine *co) {
    void *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    if (stack == MAP_FAILED) {
        perror("map failed");
        abort();
    }
    co->status = COROUTINE_STATUS_IDLE;
    co->jmp_env = NULL;
    co->stack = stack + STACK_SIZE;//stack grows down
    co->stack_size = STACK_SIZE;
    co->name = "<inited>";
}

static void deinit_coroutine(struct coroutine *co) {
    munmap(co->stack - co->stack_size, co->stack_size);
    co->stack = NULL;
    co->stack_size = 0;
    co->jmp_env = NULL;
    free(co);
}


struct coroutine *current_coroutine = NULL;

struct coroutine *get_current_coroutine() {
    return current_coroutine;
}

static inline void set_current_coroutine(struct coroutine *co) {
    current_coroutine = co;
}

static inline void set_coroutine_status(struct coroutine *co, enum coroutine_status status) {
    if (co == NULL) {
        return;
    }
    co->status = status;
}

struct coroutine *setup_coroutine(uint32_t max_count) {
    if (max_count <= 1) {
        return NULL;
    }
    max_coroutine_count = max_count;
    if (init_queue(&co_idle_queue, max_coroutine_count) != 0) {
        return NULL;
    }
    if (init_queue(&co_pending_queue, max_coroutine_count) != 0) {
        deinit_queue(&co_idle_queue);
        return NULL;
    }
    if (init_queue(&co_all_queue, max_coroutine_count) != 0) {
        deinit_queue(&co_idle_queue);
        deinit_queue(&co_pending_queue);
        return NULL;
    }
    struct coroutine *main_co = malloc(sizeof(struct coroutine));
    if (main_co == NULL) {
        return NULL;
    }
    init_coroutine(main_co);
    main_co->status = COROUTINE_STATUS_RUNNING;
    push_queue(&co_all_queue, main_co);
    inited_coroutine_count++;
    set_current_coroutine(main_co);
    main_co->name = "main";
    g_main_co = main_co;
    return main_co;
}

int teardown_coroutine() {
    while (queue_size(&co_idle_queue) > 0) {
        struct coroutine *co = pop_queue(&co_idle_queue);
        deinit_coroutine(co);
    }
    deinit_queue(&co_idle_queue);
    max_coroutine_count = 0;
    inited_coroutine_count = 0;
    free(g_main_co);// free main coroutine
    g_main_co = NULL;
    return 0;
}


static struct coroutine *get_idle_coroutine() {
    if (queue_size(&co_idle_queue) == 0) {
        if (inited_coroutine_count >= max_coroutine_count) {
            return NULL;
        }
        struct coroutine *co = malloc(sizeof(struct coroutine));
        if (co == NULL) {
            return NULL;
        }
        if (queue_full(&co_idle_queue)) {
            return NULL;
        }
        init_coroutine(co);
        push_queue(&co_idle_queue, co);
        push_queue(&co_all_queue, co);
        inited_coroutine_count++;
    }
    return pop_queue(&co_idle_queue);
}

static void resume_coroutine(struct coroutine *co) {
    if (co == NULL || co->jmp_env == NULL) {
        printf("%s: co == NULL || co->jmp_env == NULL\n", __func__);
        abort();
    }
    struct coroutine *current_co = get_current_coroutine();
    if (co == current_co) {
        printf("%s: co == current_co\n", __func__);
        abort();
    }
    jmp_buf env;
    if (setjmp(env)) {
        set_coroutine_status(current_co, COROUTINE_STATUS_RUNNING);
        current_co->jmp_env = NULL;
        return;
    }
    current_co->jmp_env = env;
    set_current_coroutine(co);
    longjmp(co->jmp_env, 1);
}

struct coroutine *co_get_pending() {
    return pop_queue(&co_pending_queue);
}

void co_pending() {
    set_coroutine_status(get_current_coroutine(), COROUTINE_STATUS_PENDING);
    if (queue_size(&co_pending_queue) == 0) {
        printf("queue_size(&co_pending_queue) == 0\n");
        abort();
    }
    struct coroutine *co = pop_queue(&co_pending_queue);
    printf("co_name = %s, co = %p, current = %p\n", co->name, co, current_coroutine);
    if (co == get_current_coroutine()) {
        printf("co == get_current_coroutine()\n");
        abort();
    }
    push_queue(&co_pending_queue, get_current_coroutine());
    resume_coroutine(co);
}

void co_block() {
    printf("co_block()\n");
    set_coroutine_status(get_current_coroutine(), COROUTINE_STATUS_BLOCKED);
    if (queue_size(&co_pending_queue) > 0) {
        resume_coroutine(pop_queue(&co_pending_queue));
        return;
    }
    printf("all coroutine blocked\n");
    abort();
}

__attribute__((noinline))_Noreturn static void
modify_stack_pointer_and_jmp_tp_coroutine(struct coroutine *co, coroutine_func func, void *arg) {
    // 一定不能inline，因为这个函数会修改栈指针，如果inline了，很多变量的读取就会出错
    // 根据调用约定，前6个整型参数均通过寄存器传递，所以不需要担心参数问题，进入该函数第一件事就是调整栈指针
    // 假装这里有16字节的空间，将栈指针和帧指针区分开来
    // 16字节而不是8字节是因为要16字节对齐，否则会出现段错误，因为 movaps 指令要求操作数是16字节对齐的
    __asm__ __volatile__(
            "movq %0, %%rsp\n\t"   // load stack pointer to rsp
            "subq $0x20, %%rsp\n\t"  // make space for 16 bytes
            :"+r"(co->stack)::);
    struct coroutine *old_co = get_current_coroutine();
    set_coroutine_status(co, COROUTINE_STATUS_PENDING);
    set_current_coroutine(co);
    push_queue(&co_pending_queue, co);

    resume_coroutine(old_co);
    func(arg);
    co->jmp_env = NULL;
    set_coroutine_status(co, COROUTINE_STATUS_IDLE);

    push_queue(&co_idle_queue, co);

    struct coroutine *next_co = pop_queue(&co_pending_queue);
    if (next_co == NULL) {
        printf("next_co == NULL\n");
        abort();
    }
    if (next_co == co) {
        printf("next_co == co\n");
        abort();
    }
    set_current_coroutine(next_co);
    longjmp(next_co->jmp_env, 1);
}

bool co_has_pending() {
    return queue_size(&co_pending_queue) > 0;
}

void co_clear_block(struct coroutine *co) {
    if (co->status != COROUTINE_STATUS_BLOCKED) {
        printf("co->status(%d) != COROUTINE_STATUS_PENDING\n", co->status);
        abort();
    }
    if (queue_full(&co_pending_queue)) {
        printf("queue_full(&co_pending_queue)\n");
        abort();
    }


    co->status = COROUTINE_STATUS_PENDING;
    push_queue(&co_pending_queue, co);
}

int new_coroutine(coroutine_func func, void *arg) {
    struct coroutine *co = get_idle_coroutine();
    if (co == NULL) {
        return -1;
    }

    co->name = "coroutine";
    jmp_buf env;
    get_current_coroutine()->jmp_env = env;
    if (setjmp(env)) {
        return 0;
    }
    modify_stack_pointer_and_jmp_tp_coroutine(co, func, arg);
}

static char *get_status_str(enum coroutine_status status) {
    switch (status) {
        case COROUTINE_STATUS_IDLE:
            return "idle";
        case COROUTINE_STATUS_RUNNING:
            return "running";
        case COROUTINE_STATUS_PENDING:
            return "pending";
        case COROUTINE_STATUS_BLOCKED:
            return "blocked";
        default:
            return "unknown";
    }
}

void print_all_coroutine() {
    printf("idle queue size = %d\n", queue_size(&co_idle_queue));
    printf("pending queue size = %d\n", queue_size(&co_pending_queue));
    printf("max_coroutine_count = %d\n", max_coroutine_count);
    printf("inited_coroutine_count = %d\n", inited_coroutine_count);
    printf("current_coroutine = %p\n", current_coroutine);
    printf("g_main_co = %p\n", g_main_co);
    for (uint32_t i = 0; i < queue_size(&co_idle_queue); i++) {
        uint32_t pos = queue_cvt_pos(&co_idle_queue, i);
        printf("    idle[%d] = %p\n", i, co_idle_queue.coroutines[pos]);
    }
    for (uint32_t i = 0; i < queue_size(&co_pending_queue); i++) {
        uint32_t pos = queue_cvt_pos(&co_pending_queue, i);
        printf("    pending[%d] = %p\n", i, co_pending_queue.coroutines[pos]);
    }
    for (uint32_t i = 0; i < queue_size(&co_all_queue); i++) {
        uint32_t pos = queue_cvt_pos(&co_all_queue, i);
        struct coroutine *co = co_all_queue.coroutines[pos];
        printf("    all[%d]  %s (%s)\n", i, co->name, get_status_str(co->status));
    }
}