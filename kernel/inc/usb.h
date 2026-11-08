#ifndef DESOS_USB_H
#define DESOS_USB_H

#include <stdint.h>

// C.6 USB：xHCI 主机控制器 + USB HID 键鼠。
// 最小可用子集：探测/复位/命令环+事件环、端口枚举、Enable Slot + Address Device、
// EP0 控制传输读描述符、识别 HID 键鼠并用 boot 协议解码，经 timer tick 非阻塞轮询
// HID 中断端点，把键鼠事件喂入既有输入层（shell_input_push / mouse_inject_rel）。

void usb_init(void);        // 探测 xHCI、初始化并枚举端口上的设备
int  usb_present(void);     // 是否找到 xHCI 控制器
int  usb_device_count(void);// 已成功寻址的设备数
int  usb_hid_count(void);   // 识别到的 HID 设备数
void usb_poll(void);        // 非阻塞：处理事件环、重投 HID 中断传输（timer tick 调用）

#endif // DESOS_USB_H
