// hello.c —— DesOS 第一个用户态程序（B.10 验证用，ring3）
//
// 不依赖 libc：直接用 int 0x80 系统调用写串口并退出。
// 由 tools/build_user.py 交叉编译成静态 ELF64，镜像随内核一起打进 ISO。

typedef unsigned long u64;

static inline u64 sys_call(u64 num, u64 a1, u64 a2, u64 a3) {
    u64 r = num;
    __asm__ volatile ("int $0x80"
                      : "+a"(r)
                      : "D"(a1), "S"(a2), "d"(a3)
                      : "memory");
    return r;
}

void _start(void) {
    static const char msg[] = "Hello from DesOS userland (ring3)!\n";
    sys_call(0, 1, (u64)msg, sizeof(msg) - 1);   // SYS_WRITE(1, msg, len)
    sys_call(2, 0, 0, 0);                         // SYS_EXIT
    for (;;) { }                                  // 正常不会走到这里（ring3 不能 hlt）
}
