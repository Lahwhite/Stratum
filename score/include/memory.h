#pragma once

#include <cstdint>

// 读取物理地址处的数据（1/2/4 字节）
uint32_t paddr_read(uint32_t addr, int len);

// 写入物理地址处的数据
void paddr_write(uint32_t addr, int len, uint32_t data);