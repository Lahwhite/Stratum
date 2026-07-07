#pragma once

#include <cstdint>

// 物理内存起始地址（RISC-V 裸机约定从 0x80000000 开始）
#define PMEM_BASE  0x80000000u
// 物理内存大小：128 MB
#define PMEM_SIZE  (128 * 1024 * 1024u)

// 将物理地址转换为宿主机指针
uint8_t* guest_to_host(uint32_t paddr);

// 读取物理地址处的数据（1/2/4 字节）
uint32_t paddr_read(uint32_t addr, int len);

// 写入物理地址处的数据
void paddr_write(uint32_t addr, int len, uint32_t data);