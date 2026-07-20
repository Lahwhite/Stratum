#pragma once

// 精简版 ELF32 结构体定义。
//
// 背景：系统标准 <elf.h> 是 Linux/glibc 提供的头文件，macOS（Mach-O 可执行文件格式）
// 不自带这个头文件，直接 #include <elf.h> 会编译失败（file not found）。
// 为了让 SCore 在 macOS / Linux 上都能编译，这里只手写 ELF 加载器实际用到的
// 最小结构体和常量集合，不依赖操作系统是否提供标准 ELF 头文件。
//
// 参考标准：System V Application Binary Interface / ELF32 规范。

#include <cstdint>

// ─── 基础类型（与标准 elf.h 命名保持一致，便于对照规范文档）──────────────
using Elf32_Addr  = uint32_t;
using Elf32_Off   = uint32_t;
using Elf32_Half   = uint16_t;
using Elf32_Word  = uint32_t;
using Elf32_Sword = int32_t;

// ─── e_ident 数组下标 ──────────────────────────────────────────────────
#define EI_MAG0    0   // 魔数字节 0：固定 0x7f
#define EI_MAG1    1   // 魔数字节 1：固定 'E'
#define EI_MAG2    2   // 魔数字节 2：固定 'L'
#define EI_MAG3    3   // 魔数字节 3：固定 'F'
#define EI_CLASS   4   // 文件类型：32 位 / 64 位
#define EI_DATA    5   // 字节序：小端 / 大端
#define EI_NIDENT  16  // e_ident 数组总长度

#define ELFMAG0    0x7f
#define ELFMAG1    'E'
#define ELFMAG2    'L'
#define ELFMAG3    'F'

#define ELFCLASS32 1   // 32 位 ELF
#define ELFCLASS64 2   // 64 位 ELF

#define ELFDATA2LSB 1  // 小端序

// ─── e_machine（目标架构）──────────────────────────────────────────────
#define EM_RISCV   243 // RISC-V

// ─── e_type（文件类型）─────────────────────────────────────────────────
#define ET_EXEC    2   // 可执行文件

// ─── ELF32 文件头（52 字节）────────────────────────────────────────────
struct Elf32_Ehdr {
    unsigned char e_ident[EI_NIDENT]; // 魔数 + 平台标识
    Elf32_Half    e_type;             // 文件类型
    Elf32_Half    e_machine;          // 目标架构
    Elf32_Word    e_version;          // 版本
    Elf32_Addr    e_entry;            // 程序入口地址
    Elf32_Off     e_phoff;            // Program Header Table 文件偏移
    Elf32_Off     e_shoff;            // Section Header Table 文件偏移
    Elf32_Word    e_flags;            // 平台相关标志
    Elf32_Half    e_ehsize;           // ELF Header 自身大小
    Elf32_Half    e_phentsize;        // 每个 Program Header 的大小
    Elf32_Half    e_phnum;            // Program Header 数量
    Elf32_Half    e_shentsize;        // 每个 Section Header 的大小
    Elf32_Half    e_shnum;            // Section Header 数量
    Elf32_Half    e_shstrndx;         // 字符串表所在的 Section 索引
};

// ─── p_type（Program Header 类型）──────────────────────────────────────
#define PT_NULL    0
#define PT_LOAD    1   // 需要被加载到内存的段
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4

// ─── ELF32 Program Header（32 字节）────────────────────────────────────
struct Elf32_Phdr {
    Elf32_Word p_type;   // 段类型（我们只关心 PT_LOAD）
    Elf32_Off  p_offset; // 段数据在文件中的偏移
    Elf32_Addr p_vaddr;  // 段的虚拟地址
    Elf32_Addr p_paddr;  // 段的物理地址（裸机场景下与 vaddr 相同，加载时使用这个）
    Elf32_Word p_filesz; // 段在文件中的字节数
    Elf32_Word p_memsz;  // 段在内存中的字节数（大于 p_filesz 的部分是 BSS，需清零）
    Elf32_Word p_flags;  // 权限标志（可读/可写/可执行）
    Elf32_Word p_align;  // 对齐要求
};

// ─── 对外接口 ──────────────────────────────────────────────────────────
// 加载 ELF 文件到内存，设置 cpu.pc，返回是否成功。
// 函数定义在 src/monitor/elf.cpp 中。
bool load_elf(const char* path);
