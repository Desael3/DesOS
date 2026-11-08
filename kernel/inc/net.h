#ifndef DESOS_NET_H
#define DESOS_NET_H

#include <stdint.h>

// C.5 网络：Intel e1000（8254x 系列）PCI 以太网控制器驱动
// 最小可用子集：探测 + MMIO 映射 + 复位 + 读 MAC + 链路 UP + TX/RX 描述符环。
// 自检用「发送一帧并等待 TX 完成（DD）」验证数据通路（不依赖外部网络/回环）。

void net_init(void);                 // 探测 e1000、映射寄存器、初始化收发环
int  net_present(void);              // 是否找到以太网控制器（PCI class 0x02,00）
void net_get_mac(uint8_t mac[6]);   // 读取 MAC 地址（6 字节）
int  net_link_up(void);               // 链路是否 UP（STATUS.LU）
int  net_loopback_test(void);        // 发送一帧并验证 TX 数据通路，返回 0=成功 -1=失败

#endif // DESOS_NET_H
