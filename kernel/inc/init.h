#ifndef DESOS_INIT_H
#define DESOS_INIT_H
// D.3b：开机拉起用户态 init（PRIV_SYSTEM），等待其 SYS_EXIT 后返回
void init_launch(void);
#endif // DESOS_INIT_H