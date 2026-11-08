// ata.c —— C.4 磁盘驱动：传统 IDE（ATA PIO，主盘 0x1F0），LBA28。
//
// 仅实现主盘（bus0 master）的 PIO 路径：identify、读/写扇区。不碰
// 从盘、不碰 ATAPI（光驱走 0x170，由 Limine 引导用）。当前 QEMU 默认
// PIIX3 IDE（00:01.1），加 -hda 后 0x1F0 即硬盘。

#include "ata.h"
#include "kprintf.h"
#include "pci.h"
#include <stdint.h>

#define IO_BASE 0x1F0
#define IO_CTRL 0x3F6   // 主通道 Device Control / Alternate Status

// 状态寄存器位
#define SR_ERR   0x01
#define SR_DRQ   0x08
#define SR_DF    0x20
#define SR_DRDY  0x40
#define SR_BSY   0x80

static inline uint8_t inb(uint16_t p){ uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void outb(uint16_t p, uint8_t v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline uint16_t inw(uint16_t p){ uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void outw(uint16_t p, uint16_t v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }

static int      g_present;
static char     g_model[41];
static uint64_t g_sectors;

static void insw(uint16_t port, void *buf, int words){
    uint16_t *p = (uint16_t *)buf;
    for (int i = 0; i < words; i++) p[i] = inw(port);
}
static void outsw(uint16_t port, const void *buf, int words){
    const uint16_t *p = (const uint16_t *)buf;
    for (int i = 0; i < words; i++) outw(port, p[i]);
}

// 忙等待（粗略延迟）：约若干微秒，用于命令间的稳定间隔。
static void ata_delay(int n){
    for (volatile int i = 0; i < n; i++) ;
}

// 等 BSY 清、且 (set 位全置 / clr 位全清)；超时返回 -1。
static int ata_poll(uint8_t set, uint8_t clr, unsigned timeout){
    for (unsigned i = 0; i < timeout; i++){
        uint8_t s = inb(IO_BASE + 7);
        if (s & SR_ERR)  return -2;   // 设备报错
        if (s & SR_DF)   return -3;   // 设备故障
        if ((s & set) == set && (s & clr) == 0) return 0;
    }
    return -1;   // 超时
}

// 选主盘（LBA 模式，DEV=0），并等其就绪。
static int ata_select_master(void){
    outb(IO_BASE + 6, 0xE0);
    ata_delay(1000);
    return ata_poll(0, SR_BSY, 1000000);
}

int ata_init(void){
    g_present = 0;
    g_model[0] = 0;
    g_sectors = 0;

    // 1) 确保 IDE 控制器（00:01.1）的 IO 空间已使能。
    uint32_t pcicmd = pci_read32(0, 1, 1, 0x04);
    kprintf("[C.4] ata: pci-cmd=%x\n", (int)pcicmd);
    if (!(pcicmd & 0x1)) { pci_write32(0, 1, 1, 0x04, pcicmd | 0x1); kprintf("[C.4] ata: io-enabled\n"); }

    // 2) 先直接等主盘就绪（SeaBIOS 已做过上电复位，正常情况下 BSY 应已清）。
    uint8_t s0 = inb(IO_BASE + 7);
    kprintf("[C.4] ata: status0=%d\n", (int)s0);
    int ready = ata_select_master();

    // 3) 若仍忙，做一次软件复位（SRST）再等。
    if (ready != 0) {
        kprintf("[C.4] ata: not-ready(r=%d), do SRST\n", ready);
        outb(IO_CTRL, 0x04);          // 断言 SRST（同时 nIEN=0）
        ata_delay(500000);
        outb(IO_CTRL, 0x00);          // 释放 SRST
        ata_delay(500000);
        (void)inb(IO_CTRL);           // 读 Alternate Status，冲刷复位
        ata_delay(1000);
        ready = ata_select_master();
    }
    kprintf("[C.4] ata: ready=%d status=%d\n", ready, (int)inb(IO_BASE + 7));
    if (ready != 0) return -1;

    // 4) IDENTIFY DEVICE（0xEC）。
    outb(IO_BASE + 7, 0xEC);
    ata_delay(400);
    uint8_t st = inb(IO_BASE + 7);
    if (st == 0) {                    // 0 = 该通道无设备
        kprintf("[C.4] ata: no device (status=0)\n");
        return -1;
    }
    // 等 BSY 清且 DRQ 置（数据就绪）；若 ERR 置，可能是 ATAPI 设备。
    int r = ata_poll(SR_DRQ, SR_BSY, 1000000);
    if (r != 0) {
        kprintf("[C.4] ata: identify failed (r=%d status=%d)\n", r, (int)inb(IO_BASE + 7));
        return -1;
    }

    uint16_t id[256];
    insw(IO_BASE, id, 256);
    (void)inb(IO_BASE + 7);          // 收尾状态读，清中断

    // 型号：word 27..46，每字内字节需交换（ATA 以 little-endian 存字符对）。
    for (int i = 0; i < 20; i++){
        uint16_t w = id[27 + i];
        g_model[i * 2]     = (char)(w >> 8);
        g_model[i * 2 + 1] = (char)(w & 0xFF);
    }
    g_model[40] = 0;

    // LBA28 容量：word 60（低 16）+ word 61（高 16）。
    g_sectors = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    g_present = 1;
    kprintf("[C.4] ata: identified model=%s sectors=%u\n",
            g_model[0] ? g_model : "(empty)", (uint32_t)g_sectors);
    return 0;
}

int ata_present(void){ return g_present; }

void ata_model_string(char *out, int max){
    int n = (max - 1 < 40) ? (max - 1) : 40;
    for (int i = 0; i < n; i++) out[i] = g_model[i];
    out[n] = 0;
}

uint64_t ata_total_sectors(void){ return g_sectors; }

int ata_read_sectors(uint64_t lba, uint32_t count, void *buf){
    if (!g_present) return -1;
    if (ata_select_master() != 0) return -1;
    outb(IO_BASE + 2, (uint8_t)count);
    outb(IO_BASE + 3, (uint8_t)(lba & 0xFF));
    outb(IO_BASE + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(IO_BASE + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(IO_BASE + 6, 0xE0 | (uint8_t)((lba >> 24) & 0x0F));
    outb(IO_BASE + 7, 0x20);          // READ SECTORS
    uint8_t *p = (uint8_t *)buf;
    for (uint32_t s = 0; s < count; s++){
        if (ata_poll(SR_DRQ, SR_BSY, 1000000) != 0) return -1;
        insw(IO_BASE, p, 256);
        p += 512;
    }
    (void)inb(IO_BASE + 7);
    return 0;
}

int ata_write_sectors(uint64_t lba, uint32_t count, const void *buf){
    if (!g_present) return -1;
    if (ata_select_master() != 0) return -1;
    outb(IO_BASE + 2, (uint8_t)count);
    outb(IO_BASE + 3, (uint8_t)(lba & 0xFF));
    outb(IO_BASE + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(IO_BASE + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(IO_BASE + 6, 0xE0 | (uint8_t)((lba >> 24) & 0x0F));
    outb(IO_BASE + 7, 0x30);          // WRITE SECTORS
    const uint8_t *p = (const uint8_t *)buf;
    for (uint32_t s = 0; s < count; s++){
        if (ata_poll(SR_DRQ, SR_BSY, 1000000) != 0) return -1;
        outsw(IO_BASE, p, 256);
        p += 512;
        if (ata_poll(0, SR_BSY | SR_ERR | SR_DF, 1000000) != 0) return -1;
    }
    (void)inb(IO_BASE + 7);
    return 0;
}
