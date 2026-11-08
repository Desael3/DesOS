#ifndef DESOS_ATA_H
#define DESOS_ATA_H

#include <stdint.h>

#define ATA_SECTOR_SIZE 512

// C.4 磁盘驱动：传统 IDE（ATA PIO，主盘 IO 基址 0x1F0），LBA28。
int      ata_init(void);              // 探测主盘并 identify（结果缓存），返回 0 成功
int      ata_present(void);           // 是否识别到 ATA 硬盘
void     ata_model_string(char *out, int max); // 型号字符串（已转字节序）
uint64_t ata_total_sectors(void);     // LBA28 容量（扇区数）
int      ata_read_sectors(uint64_t lba, uint32_t count, void *buf);  // 0=成功
int      ata_write_sectors(uint64_t lba, uint32_t count, const void *buf);

#endif // DESOS_ATA_H
