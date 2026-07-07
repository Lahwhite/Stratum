#include <cstdio>
#include <cstring>

#include "../../include/elf.h"
#include "../../include/memory.h"
#include "../../include/cpu.h"

// 加载 ELF 文件到内存，设置 cpu.pc，返回是否成功
bool load_elf(const char* path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("无法打开文件：%s\n", path);
        return false;
    }

    // 1. 读取 ELF Header
    Elf32_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, f) != 1) {
        printf("读取 ELF Header 失败：%s\n", path);
        fclose(f);
        return false;
    }

    // 2. 验证魔数
    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 || ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        printf("不是合法的 ELF 文件：%s\n", path);
        fclose(f);
        return false;
    }

    // 3. 验证是 32 位
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS32) {
        printf("不是 32 位 ELF 文件：%s\n", path);
        fclose(f);
        return false;
    }

    // 4. 验证是 RISC-V
    if (ehdr.e_machine != EM_RISCV) {
        printf("不是 RISC-V 架构的 ELF 文件：%s\n", path);
        fclose(f);
        return false;
    }

    // 5. 设置程序入口地址
    cpu.pc = ehdr.e_entry;

    // 6. 遍历所有 Program Header，加载 PT_LOAD 段
    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf32_Phdr phdr;
        if (fseek(f, ehdr.e_phoff + i * sizeof(Elf32_Phdr), SEEK_SET) != 0) {
            printf("定位 Program Header 失败：index=%d\n", i);
            fclose(f);
            return false;
        }
        if (fread(&phdr, sizeof(phdr), 1, f) != 1) {
            printf("读取 Program Header 失败：index=%d\n", i);
            fclose(f);
            return false;
        }

        if (phdr.p_type != PT_LOAD) continue;

        if (fseek(f, phdr.p_offset, SEEK_SET) != 0) {
            printf("定位段数据失败：p_offset=0x%x\n", phdr.p_offset);
            fclose(f);
            return false;
        }

        uint8_t *host_ptr = guest_to_host(phdr.p_paddr);
        if (fread(host_ptr, 1, phdr.p_filesz, f) != phdr.p_filesz) {
            printf("读取段数据失败：p_paddr=0x%08x\n", phdr.p_paddr);
            fclose(f);
            return false;
        }

        // BSS 段：内存占用大于文件占用的部分，用 0 填充
        if (phdr.p_memsz > phdr.p_filesz) {
            memset(host_ptr + phdr.p_filesz, 0, phdr.p_memsz - phdr.p_filesz);
        }
    }

    fclose(f);
    return true;
}
