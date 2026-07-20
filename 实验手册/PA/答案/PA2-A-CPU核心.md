# PA2-A — SCore CPU 核心
> 成员 A 独立完成 · 前置：联调1 完成（v1.0-pa1 已打 tag）· 预计工时：6～8 天

---

## 本手册范围说明

本手册仅包含**成员 A 的工作**：

- 物理内存模块（替换 PA1 中的桩）
- ELF 加载器
- RISC-V 32 位取指-译码-执行（IFE）主循环
- RV32I 全部 47 条指令实现
- 可选：RV32M 乘除法扩展（8 条）
- `cpu_exec(n)` 接口，供 SDB 的 `si` 命令调用

**不包含**：klib 标准库、SHAL 抽象层——这些由成员 B 在 [PA2-B] 中完成。

**前置可用成果（来自联调1）：**
- SDB 完整功能（`si`/`p`/`w` 等）均已可用，A 可直接用 SDB 调试 CPU

---

## 目标

**本阶段完成标志：**
- 能加载并执行一个最简单的 RISC-V 裸机二进制（只有几条指令，以 `ebreak` 结束）
- `cpu-tests` 所有测试用例通过
- SDB 的 `si` 命令能真正单步执行 RISC-V 指令并更新寄存器

---

## 一、物理内存模块

替换 PA1 中的空桩实现。

### 1.1 内存配置

修改 `score/include/memory.h`，增加配置宏：

```cpp
#pragma once
#include <cstdint>

// 物理内存起始地址（RISC-V 裸机约定从 0x80000000 开始）
#define PMEM_BASE  0x80000000u
// 物理内存大小：128 MB
#define PMEM_SIZE  (128 * 1024 * 1024u)

// 将物理地址转换为宿主机指针
uint8_t* guest_to_host(uint32_t paddr);

// 读写接口（len = 1/2/4 字节）
uint32_t paddr_read (uint32_t addr, int len);
void     paddr_write(uint32_t addr, int len, uint32_t data);
```

### 1.2 内存实现

修改 `score/src/memory/memory.cpp`：

```cpp
#include "memory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

static uint8_t pmem[PMEM_SIZE];

uint8_t* guest_to_host(uint32_t paddr) {
    assert(paddr >= PMEM_BASE && paddr < PMEM_BASE + PMEM_SIZE);
    return pmem + (paddr - PMEM_BASE);
}

uint32_t paddr_read(uint32_t addr, int len) {
    if (addr < PMEM_BASE || addr + len > PMEM_BASE + PMEM_SIZE) {
        fprintf(stderr, "非法内存读：addr=0x%08x len=%d\n", addr, len);
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
    if (addr < PMEM_BASE || addr + len > PMEM_BASE + PMEM_SIZE) {
        fprintf(stderr, "非法内存写：addr=0x%08x len=%d data=0x%08x\n", addr, len, data);
        return;
    }
    uint8_t *p = guest_to_host(addr);
    switch (len) {
        case 1: *p              = (uint8_t) data; break;
        case 2: *(uint16_t*)p  = (uint16_t)data; break;
        case 4: *(uint32_t*)p  = (uint32_t)data; break;
        default: assert(0);
    }
}

// 加载镜像到内存（由 ELF 加载器或直接加载 .bin 调用）
void load_image(const void *img, uint32_t load_addr, size_t size) {
    assert(load_addr >= PMEM_BASE);
    assert(load_addr + size <= PMEM_BASE + PMEM_SIZE);
    memcpy(guest_to_host(load_addr), img, size);
}
```

---

## 二、ELF 加载器

创建 `score/src/utils/loader.cpp`：

```cpp
#include "memory.h"
#include "cpu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <elf.h>    // Linux 系统头，定义 Elf32_Ehdr / Elf32_Phdr

// 从文件加载 ELF，返回入口地址；失败返回 0
uint32_t load_elf(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 0; }

    // 读取 ELF header
    Elf32_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, f) != 1) goto err;

    // 检查 magic
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "%s: 不是合法的 ELF 文件\n", path);
        goto err;
    }
    if (ehdr.e_machine != EM_RISCV) {
        fprintf(stderr, "%s: 不是 RISC-V ELF\n", path);
        goto err;
    }
    if (ehdr.e_class != ELFCLASS32) {
        fprintf(stderr, "%s: 不是 32 位 ELF\n", path);
        goto err;
    }

    // 遍历 Program Header，加载 LOAD 段
    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf32_Phdr phdr;
        fseek(f, ehdr.e_phoff + i * ehdr.e_phentsize, SEEK_SET);
        if (fread(&phdr, sizeof(phdr), 1, f) != 1) goto err;
        if (phdr.p_type != PT_LOAD) continue;

        // 读取文件内容
        uint8_t *buf = (uint8_t*)malloc(phdr.p_filesz);
        fseek(f, phdr.p_offset, SEEK_SET);
        if (fread(buf, 1, phdr.p_filesz, f) != phdr.p_filesz) {
            free(buf); goto err;
        }
        // 写入物理内存
        load_image(buf, phdr.p_paddr, phdr.p_filesz);
        free(buf);

        // BSS 段清零（filesz < memsz 的部分）
        if (phdr.p_memsz > phdr.p_filesz) {
            memset(guest_to_host(phdr.p_paddr + phdr.p_filesz),
                   0, phdr.p_memsz - phdr.p_filesz);
        }
    }

    fclose(f);
    printf("加载 ELF：%s  入口 = 0x%08x\n", path, ehdr.e_entry);
    return ehdr.e_entry;

err:
    fclose(f);
    return 0;
}
```

---

## 三、RISC-V 指令执行核心

### 3.1 指令格式解码辅助宏

创建 `score/include/decode.h`：

```cpp
#pragma once
#include <cstdint>

// 从 32 位指令中提取字段
#define BITS(x, hi, lo)   (((x) >> (lo)) & ((1u << ((hi)-(lo)+1)) - 1))
#define BIT(x, b)         (((x) >> (b)) & 1u)

// 各类型指令字段提取
#define OPCODE(i)   BITS(i,  6,  0)
#define RD(i)       BITS(i, 11,  7)
#define FUNCT3(i)   BITS(i, 14, 12)
#define RS1(i)      BITS(i, 19, 15)
#define RS2(i)      BITS(i, 24, 20)
#define FUNCT7(i)   BITS(i, 31, 25)

// 立即数提取（已符号扩展）
inline int32_t IMM_I(uint32_t i) {
    return (int32_t)i >> 20;   // bits[31:20]，算术右移做符号扩展
}
inline int32_t IMM_S(uint32_t i) {
    return ((int32_t)(i & 0xfe000000) >> 20) | (int32_t)BITS(i, 11, 7);
}
inline int32_t IMM_B(uint32_t i) {
    return (int32_t)(
        (BIT(i,31) << 12) | (BIT(i,7)  << 11) |
        (BITS(i,30,25) << 5) | (BITS(i,11,8) << 1)
    ) | -(int32_t)(BIT(i,31) << 12);   // 符号扩展
}
inline int32_t IMM_U(uint32_t i) {
    return (int32_t)(i & 0xfffff000u);
}
inline int32_t IMM_J(uint32_t i) {
    return (int32_t)(
        (BIT(i,31) << 20)   | (BITS(i,19,12) << 12) |
        (BIT(i,20) << 11)   | (BITS(i,30,21) << 1)
    ) | -(int32_t)(BIT(i,31) << 20);
}
```

### 3.2 CPU 执行主循环

创建 `score/src/cpu/execute.cpp`：

```cpp
#include "cpu.h"
#include "memory.h"
#include "decode.h"
#include "watchpoint.h"
#include <cstdio>
#include <cstdlib>

// CPU 运行状态
enum CpuState { CPU_RUNNING, CPU_STOPPED, CPU_ABORT };
static CpuState cpu_state = CPU_STOPPED;

// 将 CPU 置为初始状态
void cpu_reset(uint32_t entry) {
    for (int i = 0; i < 32; i++) cpu.gpr[i] = 0;
    cpu.pc = entry;
    cpu_state = CPU_RUNNING;
}

// 辅助：写寄存器（自动忽略 x0）
static inline void set_reg(int rd, uint32_t val) {
    if (rd != 0) cpu.gpr[rd] = val;
}

// 执行单条指令，返回 false 表示停机
static bool exec_once() {
    uint32_t pc   = cpu.pc;
    uint32_t inst = paddr_read(pc, 4);
    uint32_t op   = OPCODE(inst);
    uint32_t rd   = RD(inst);
    uint32_t rs1  = RS1(inst);
    uint32_t rs2  = RS2(inst);
    uint32_t f3   = FUNCT3(inst);
    uint32_t f7   = FUNCT7(inst);

    uint32_t next_pc = pc + 4;   // 默认顺序执行

    switch (op) {
    // ── LUI ──────────────────────────────────────────────────────────
    case 0x37:
        set_reg(rd, (uint32_t)IMM_U(inst));
        break;

    // ── AUIPC ────────────────────────────────────────────────────────
    case 0x17:
        set_reg(rd, pc + (uint32_t)IMM_U(inst));
        break;

    // ── JAL ──────────────────────────────────────────────────────────
    case 0x6f:
        set_reg(rd, pc + 4);
        next_pc = pc + (uint32_t)IMM_J(inst);
        break;

    // ── JALR ─────────────────────────────────────────────────────────
    case 0x67:
        { uint32_t t = pc + 4;
          next_pc = (cpu.gpr[rs1] + (uint32_t)IMM_I(inst)) & ~1u;
          set_reg(rd, t); }
        break;

    // ── BRANCH ───────────────────────────────────────────────────────
    case 0x63: {
        int32_t  imm    = IMM_B(inst);
        uint32_t src1   = cpu.gpr[rs1];
        uint32_t src2   = cpu.gpr[rs2];
        bool     taken  = false;
        switch (f3) {
            case 0x0: taken = src1 == src2; break;              // BEQ
            case 0x1: taken = src1 != src2; break;              // BNE
            case 0x4: taken = (int32_t)src1 <  (int32_t)src2; break; // BLT
            case 0x5: taken = (int32_t)src1 >= (int32_t)src2; break; // BGE
            case 0x6: taken = src1 <  src2; break;              // BLTU
            case 0x7: taken = src1 >= src2; break;              // BGEU
            default:  goto illegal;
        }
        if (taken) next_pc = pc + (uint32_t)imm;
        break;
    }

    // ── LOAD ─────────────────────────────────────────────────────────
    case 0x03: {
        uint32_t addr = cpu.gpr[rs1] + (uint32_t)IMM_I(inst);
        uint32_t val;
        switch (f3) {
            case 0x0: val = (int32_t)(int8_t) paddr_read(addr, 1); break; // LB
            case 0x1: val = (int32_t)(int16_t)paddr_read(addr, 2); break; // LH
            case 0x2: val =                   paddr_read(addr, 4); break;  // LW
            case 0x4: val =            (uint8_t) paddr_read(addr, 1); break; // LBU
            case 0x5: val =            (uint16_t)paddr_read(addr, 2); break; // LHU
            default:  goto illegal;
        }
        set_reg(rd, val);
        break;
    }

    // ── STORE ────────────────────────────────────────────────────────
    case 0x23: {
        uint32_t addr = cpu.gpr[rs1] + (uint32_t)IMM_S(inst);
        switch (f3) {
            case 0x0: paddr_write(addr, 1, cpu.gpr[rs2]); break; // SB
            case 0x1: paddr_write(addr, 2, cpu.gpr[rs2]); break; // SH
            case 0x2: paddr_write(addr, 4, cpu.gpr[rs2]); break; // SW
            default:  goto illegal;
        }
        break;
    }

    // ── OP-IMM（I 型运算）─────────────────────────────────────────────
    case 0x13: {
        int32_t  imm  = IMM_I(inst);
        uint32_t src  = cpu.gpr[rs1];
        uint32_t shamt = rs2;   // imm[4:0]
        uint32_t res  = 0;
        switch (f3) {
            case 0x0: res = src + (uint32_t)imm;          break; // ADDI
            case 0x2: res = (int32_t)src < imm ? 1 : 0;  break; // SLTI
            case 0x3: res = src < (uint32_t)imm ? 1 : 0; break; // SLTIU
            case 0x4: res = src ^ (uint32_t)imm;          break; // XORI
            case 0x6: res = src | (uint32_t)imm;          break; // ORI
            case 0x7: res = src & (uint32_t)imm;          break; // ANDI
            case 0x1: res = src << shamt;                  break; // SLLI
            case 0x5:
                if (f7 == 0x00) res = src >> shamt;              // SRLI
                else            res = (int32_t)src >> shamt;      // SRAI
                break;
            default: goto illegal;
        }
        set_reg(rd, res);
        break;
    }

    // ── OP（R 型运算）────────────────────────────────────────────────
    case 0x33: {
        uint32_t s1 = cpu.gpr[rs1];
        uint32_t s2 = cpu.gpr[rs2];
        uint32_t res = 0;
        if (f7 == 0x00) {
            switch (f3) {
                case 0x0: res = s1 + s2;                         break; // ADD
                case 0x1: res = s1 << (s2 & 0x1f);              break; // SLL
                case 0x2: res = (int32_t)s1 < (int32_t)s2 ? 1:0; break; // SLT
                case 0x3: res = s1 < s2 ? 1 : 0;                break; // SLTU
                case 0x4: res = s1 ^ s2;                         break; // XOR
                case 0x5: res = s1 >> (s2 & 0x1f);              break; // SRL
                case 0x6: res = s1 | s2;                         break; // OR
                case 0x7: res = s1 & s2;                         break; // AND
                default: goto illegal;
            }
        } else if (f7 == 0x20) {
            switch (f3) {
                case 0x0: res = s1 - s2;                          break; // SUB
                case 0x5: res = (int32_t)s1 >> (s2 & 0x1f);     break; // SRA
                default: goto illegal;
            }
        } else if (f7 == 0x01) {
            // RV32M 扩展
            switch (f3) {
                case 0x0: res = s1 * s2;                                    break; // MUL
                case 0x1: res = (uint32_t)((int64_t)(int32_t)s1*(int32_t)s2 >> 32); break; // MULH
                case 0x2: res = (uint32_t)((int64_t)(int32_t)s1*(uint64_t)s2 >> 32); break; // MULHSU
                case 0x3: res = (uint32_t)((uint64_t)s1*(uint64_t)s2 >> 32);         break; // MULHU
                case 0x4: res = s2 == 0 ? 0xffffffff : (uint32_t)((int32_t)s1/(int32_t)s2); break; // DIV
                case 0x5: res = s2 == 0 ? 0xffffffff : s1/s2;              break; // DIVU
                case 0x6: res = s2 == 0 ? s1 : (uint32_t)((int32_t)s1%(int32_t)s2); break; // REM
                case 0x7: res = s2 == 0 ? s1 : s1%s2;                     break; // REMU
                default: goto illegal;
            }
        } else goto illegal;
        set_reg(rd, res);
        break;
    }

    // ── SYSTEM ───────────────────────────────────────────────────────
    case 0x73:
        if (inst == 0x00100073) {
            // EBREAK：停机（用于测试程序结束）
            cpu_state = CPU_STOPPED;
            return false;
        }
        // ECALL 在 PA4 实现，此处先忽略
        break;

    // ── FENCE（nop 实现即可）──────────────────────────────────────────
    case 0x0f:
        break;

    default:
    illegal:
        fprintf(stderr, "非法指令：pc=0x%08x inst=0x%08x\n", pc, inst);
        cpu_state = CPU_ABORT;
        return false;
    }

    cpu.pc = next_pc;
    return true;
}

// 对外接口：执行 n 条指令（n=0 表示一直执行到停机）
void cpu_exec(uint64_t n) {
    if (cpu_state == CPU_STOPPED) {
        printf("程序已停机，请重新加载\n");
        return;
    }
    for (uint64_t i = 0; n == 0 || i < n; i++) {
        if (!exec_once()) break;
        // 每条指令后检查 watchpoint（联调1 已实现）
        if (wp_check()) {
            cpu_state = CPU_STOPPED;
            break;
        }
    }
}
```

### 3.3 更新 SDB 的 si 命令

回到 `sdb.cpp`，将 `cmd_si` 中的 stub 替换为真实调用：

```cpp
#include "execute.h"   // 添加此 include（新建 execute.h 声明 cpu_exec/cpu_reset）

static int cmd_si(char *args) {
    int n = 1;
    if (args != nullptr) n = atoi(args);
    if (n <= 0) n = 1;
    cpu_exec((uint64_t)n);
    return 0;
}
```

创建 `score/include/execute.h`：

```cpp
#pragma once
#include <cstdint>

void cpu_reset(uint32_t entry);
void cpu_exec(uint64_t n);
```

---

## 四、命令行参数解析与程序加载

修改 `score/src/main.cpp`，支持加载 ELF 文件：

```cpp
#include <cstdio>
#include <cstring>
#include "sdb.h"
#include "execute.h"

// loader.cpp 中定义
extern uint32_t load_elf(const char *path);

int main(int argc, char *argv[]) {
    printf("Stratum-Core (SCore) - RISC-V 32-bit Simulator\n");
    printf("Build: " __DATE__ " " __TIME__ "\n\n");

    uint32_t entry = 0x80000000u;   // 默认入口

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-e") == 0 && i+1 < argc) {
            entry = load_elf(argv[++i]);
            if (entry == 0) return 1;
        }
    }

    cpu_reset(entry);
    sdb_mainloop();
    return 0;
}
```

用法：`./score/score -e path/to/program.elf`

---

## 五、cpu-tests 测试套件

在 `stratum-apps/cpu-tests/` 目录下为每种指令类型准备汇编测试：

```
cpu-tests/
├── add.S      # 测试 ADD/ADDI
├── branch.S   # 测试所有分支指令
├── load.S     # 测试 LB/LH/LW/LBU/LHU
├── store.S    # 测试 SB/SH/SW
├── lui.S      # 测试 LUI/AUIPC
├── jal.S      # 测试 JAL/JALR
├── shift.S    # 测试移位指令
└── ...
```

每个测试程序约定：**最后执行 `ebreak` 停机**，通过检查寄存器值来判断测试是否通过。

构建脚本 `cpu-tests/Makefile`：

```makefile
CC := riscv32-unknown-elf-gcc
CFLAGS := -march=rv32im -mabi=ilp32 -nostdlib -T link.ld

TESTS := $(wildcard *.S)
ELFS  := $(TESTS:.S=.elf)

all: $(ELFS)

%.elf: %.S
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(ELFS)
```

链接脚本 `cpu-tests/link.ld`：

```
SECTIONS {
    . = 0x80000000;
    .text   : { *(.text) }
    .data   : { *(.data) }
    .bss    : { *(.bss)  }
}
```

运行所有测试：

```bash
cd stratum-apps/cpu-tests && make
for f in *.elf; do
    echo -n "Testing $f ... "
    ../../score/score -e $f 2>/dev/null && echo PASS || echo FAIL
done
```

---

## 六、阶段完成检查清单

- [ ] 编译无 error
- [ ] `./score/score -e cpu-tests/add.elf` 能执行到 ebreak 停机
- [ ] SDB 的 `si` 命令真正执行 RISC-V 指令，`info r` 显示寄存器值变化
- [ ] `x` 命令能读出加载到内存的程序内容
- [ ] cpu-tests 所有测试程序通过
- [ ] 非法指令有错误提示，不崩溃
- [ ] 非法内存访问有错误提示，不崩溃

---

## 七、提交本阶段成果

```bash
git add score/
git commit -m "pa2-a: implement RV32IM CPU core and memory"
git push origin feat/pa2-cpu
```

> 不要向 `dev` 合入，等成员 B 完成 PA2-B 后，进入 [联调2] 再合并。

---

*本阶段完成后，等待成员 B 完成 [PA2-B-klib基础库]，再进入 [联调2-首次跨层联调]。*
