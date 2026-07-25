#include <cstdio>

#include "../include/sdb.h"
#include "../include/elf.h"
#include "../include/cpu.h"

int main(int argc, char *argv[]) {
    printf("Stratum-Core (SCore) - RISC-V 32-bit Simulator\n");
    if (argc < 2) {
        printf("Usage: %s <elf_file>\n", argv[0]);
        return 1;
    }
    if (!load_elf(argv[1])) {
        printf("加载 ELF 失败：%s\n", argv[1]);
        return 1;
    }

    // 初始化栈指针：指向物理内存顶端（栈从高地址向低地址增长）
    cpu.gpr[2] = PMEM_BASE + PMEM_SIZE;  // x2 = sp

    printf("ELF 已加载，入口地址：0x%08x, 栈顶：0x%08x\n", cpu.pc, cpu.gpr[2]);
    sdb_mainloop();
    return 0;
}