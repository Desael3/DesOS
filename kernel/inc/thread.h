#ifndef DESOS_THREAD_H
#define DESOS_THREAD_H

#include <stdint.h>

// B.8 进程/线程模型（内核线程，TCB）
//   - 每个线程有独立内核栈；首次启动时在栈上构造一份「中断帧」，
//     使统一的中断返回路径（isr_common 的 pop+iretq）能直接把它跑起来。
//   - 当前所有线程共用内核 PML4（cr3 字段先留好，B.10 用户态时再分家）。
typedef enum {
    THREAD_READY = 0,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_DEAD
} thread_state_t;

struct user_image;

struct thread {
    uint64_t rsp;               // 上下文切换点：被中断时的内核栈指针
    uint64_t stack_base;        // kmalloc 分配的栈基址
    uint64_t kernel_stack_top;  // 用户线程 ring3->ring0 时写入 TSS.rsp0
    uint64_t cr3;               // 地址空间（内核线程共用内核 PML4；用户线程独立）
    thread_state_t state;
    int id;
    uint8_t priv;               // D.2 权限级（priv_level_t）：内核线程=kernel，用户线程默认 user
    struct thread *next;        // 就绪队列链表
    void (*entry)(void *);
    void *arg;
    char name[16];
};

struct thread *thread_create(const char *name, void (*entry)(void *), void *arg);
struct thread *thread_create_user(const char *name, const struct user_image *img);
// D.2：以指定权限级创建用户线程（Shell 的 run [priv] / D.3 init 用）
struct thread *thread_create_user_priv(const char *name, const struct user_image *img,
                                       uint8_t priv);
struct thread *thread_current(void);
void thread_exit(void);         // 走 SYS_EXIT 系统调用，标记 DEAD 并让出
uint64_t thread_count(void);    // 已创建的内核线程数（不含 main）

#endif // DESOS_THREAD_H
