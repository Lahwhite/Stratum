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
    printf("ELF 已加载，入口地址：0x%08x\n", cpu.pc);
    sdb_mainloop();
    return 0;
}