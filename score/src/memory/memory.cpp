#include <cassert>
#include <cstdio>

#include "../../include/memory.h"

static uint8_t mem[PMEM_SIZE];

uint8_t* guest_to_host(uint32_t paddr) {
    if (!(paddr >= PMEM_BASE && paddr < PMEM_BASE + PMEM_SIZE)) {
        printf("guest_to_host: 非法物理地址 0x%08x (PMEM_BASE=0x%08x, PMEM_SIZE=0x%x)\n",
               paddr, PMEM_BASE, PMEM_SIZE);
    }
    assert(paddr >= PMEM_BASE && paddr < PMEM_BASE + PMEM_SIZE);
    return mem + (paddr - PMEM_BASE);
}

// 暂时返回 0，PA2 阶段替换为真实内存
uint32_t paddr_read(uint32_t addr, int len) {
    if (addr < PMEM_BASE || addr + len > PMEM_BASE + PMEM_SIZE) {
        printf("非法内存读：addr=0x%08x len=%d\n", addr, len);
        return 0;
    }
    uint8_t *p = guest_to_host(addr);
    switch (len) {
        case 1: return *p;
        case 2: return *(uint16_t*)p;
        case 4: return *(uint32_t*)p;
        default: assert(0);
    }
    return 0;
}

void paddr_write(uint32_t addr, int len, uint32_t data) {
    // UART TX: 向 0xa0000000 写字节 → 打印到终端
    if (addr == 0xa0000000) {
        putchar((char)data);
        fflush(stdout);
        return;
    }

    if (addr < PMEM_BASE || addr + len > PMEM_BASE + PMEM_SIZE) {
        printf("非法内存写：addr=0x%08x len=%d data=0x%08x\n", addr, len, data);
        return;
    }
    uint8_t *p = guest_to_host(addr);
    switch (len) {
        case 1: *p             = (uint8_t) data; break;
        case 2: *(uint16_t*)p  = (uint16_t)data; break;
        case 4: *(uint32_t*)p  = (uint32_t)data; break;
        default: assert(0);
    }
}