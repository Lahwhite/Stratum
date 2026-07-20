# PA4-A — SCore 中断与陷入机制
> 成员 A 独立完成 · 前置：联调3 完成（v3.0-pa3 已打 tag）· 预计工时：4～5 天

---

## 本手册范围说明

本手册仅包含**成员 A 的工作**：

- RISC-V 特权级支持（M-mode CSR 寄存器）
- `ecall` 和 `ebreak` 的陷入（trap）处理
- 时钟中断（Machine Timer Interrupt）
- 异常/中断向量机制
- 上下文保存与恢复（`__am_asm_trap` 汇编入口）
- 向 B 暴露的接口：陷入向量地址设置接口

**不包含**：SOS 系统调用处理逻辑、进程调度——这些由成员 B 在 [PA4-B] 中完成。A 只负责"硬件侧"的中断/异常分发，B 负责"软件侧"的处理函数实现。

**前置可用成果（来自联调3）：**
- 完整 SHAL IOE 接口（外设均已验证）
- 完整的 RV32IM CPU 指令实现
- 可以用 SDB 进行源码级调试

---

## 目标

**本阶段完成标志：**
- `ecall` 指令能触发 SCore 的 trap handler 调用
- `mtvec` CSR 能设置中断向量，CPU 在异常时跳转到正确地址
- 时钟中断每 10ms 触发一次
- 上下文（所有寄存器）在 trap 时正确保存并在返回时恢复
- B 能通过设置 `mtvec` 来注册自己的 trap handler

---

## 一、CSR 寄存器支持

### 1.1 CSR 定义

在 `score/include/csr.h` 中定义 M-mode CSR：

```cpp
#pragma once
#include <cstdint>

// CSR 地址（12 位）
enum CSRAddr : uint32_t {
    // Machine Information
    CSR_MVENDORID  = 0xf11,
    CSR_MARCHID    = 0xf12,
    CSR_MIMPID     = 0xf13,
    CSR_MHARTID    = 0xf14,

    // Machine Trap Setup
    CSR_MSTATUS    = 0x300,
    CSR_MISA       = 0x301,
    CSR_MIE        = 0x304,   // Machine Interrupt Enable
    CSR_MTVEC      = 0x305,   // Machine Trap-Vector Base-Address

    // Machine Trap Handling
    CSR_MSCRATCH   = 0x340,
    CSR_MEPC       = 0x341,   // Machine Exception Program Counter
    CSR_MCAUSE     = 0x342,   // Machine Cause
    CSR_MTVAL      = 0x343,   // Machine Trap Value
    CSR_MIP        = 0x344,   // Machine Interrupt Pending

    // Machine Timer（来自 CLINT）
    CSR_MTIME      = 0xc01,   // 只读（映射到 MMIO）
    CSR_MTIMECMP   = 0x321,   // 时钟中断比较寄存器（写 MMIO）
};

// mstatus 字段
#define MSTATUS_MIE   (1u <<  3)   // Machine Interrupt Enable
#define MSTATUS_MPIE  (1u <<  7)   // Previous MIE
#define MSTATUS_MPP   (3u << 11)   // Previous Privilege Mode (11 = M-mode)

// mcause 编码
#define CAUSE_MACHINE_TIMER_INT  (0x80000007u)  // 时钟中断
#define CAUSE_ECALL_FROM_M       (0x0000000bu)  // M 态 ecall
#define CAUSE_ILLEGAL_INST       (0x00000002u)
#define CAUSE_LOAD_FAULT         (0x00000005u)
#define CAUSE_STORE_FAULT        (0x00000007u)
```

### 1.2 CSR 存储

在 `score/include/cpu.h` 的 `CPU_state` 中增加 CSR 字段：

```cpp
struct CPU_state {
    uint32_t gpr[32];
    uint32_t pc;

    // M-mode CSR
    uint32_t mstatus;
    uint32_t mie;
    uint32_t mtvec;
    uint32_t mscratch;
    uint32_t mepc;
    uint32_t mcause;
    uint32_t mtval;
    uint32_t mip;
};
```

---

## 二、CSR 指令实现

在 `execute.cpp` 的 `case 0x73`（SYSTEM 指令）中扩充：

```cpp
// SYSTEM 指令
case 0x73: {
    if (inst == 0x00000073) {
        // ECALL：触发 trap
        do_trap(CAUSE_ECALL_FROM_M, 0);
        return true;
    }
    if (inst == 0x00100073) {
        // EBREAK：停机
        cpu_state = CPU_STOPPED;
        return false;
    }
    if (inst == 0x30200073) {
        // MRET：从 trap 返回
        do_mret();
        return true;
    }
    // CSR 指令（funct3 != 0）
    if (f3 != 0) {
        uint32_t csr_addr = inst >> 20;
        uint32_t &csr = get_csr(csr_addr);   // 见下文
        uint32_t old = csr;
        uint32_t src = (f3 & 4) ? rs1 : cpu.gpr[rs1];  // CSRR*I 用立即数

        switch (f3 & 3) {
            case 1: csr  =  src;         break;  // CSRRW
            case 2: csr |=  src;         break;  // CSRRS
            case 3: csr &= ~src;         break;  // CSRRC
        }
        set_reg(rd, old);
        break;
    }
    goto illegal;
}
```

CSR 访问辅助函数（在 `cpu.cpp` 中实现）：

```cpp
uint32_t& get_csr(uint32_t addr) {
    switch (addr) {
        case CSR_MSTATUS:  return cpu.mstatus;
        case CSR_MIE:      return cpu.mie;
        case CSR_MTVEC:    return cpu.mtvec;
        case CSR_MSCRATCH: return cpu.mscratch;
        case CSR_MEPC:     return cpu.mepc;
        case CSR_MCAUSE:   return cpu.mcause;
        case CSR_MTVAL:    return cpu.mtval;
        case CSR_MIP:      return cpu.mip;
        // 只读 CSR 返回临时变量（写无效）
        default:
            static uint32_t dummy = 0;
            return dummy;
    }
}
```

---

## 三、Trap 进入与退出

### 3.1 do_trap()

在 `execute.cpp` 中实现：

```cpp
static void do_trap(uint32_t cause, uint32_t tval) {
    // 1. 保存 PC 到 mepc（ecall 时保存当前 PC，返回时要跳到 PC+4）
    cpu.mepc   = cpu.pc;
    cpu.mcause = cause;
    cpu.mtval  = tval;

    // 2. 更新 mstatus：MPIE = MIE，MIE = 0（关中断），MPP = M-mode
    uint32_t mie_bit = (cpu.mstatus & MSTATUS_MIE) ? 1 : 0;
    cpu.mstatus &= ~(MSTATUS_MIE | MSTATUS_MPIE | MSTATUS_MPP);
    cpu.mstatus |= (mie_bit << 7);       // MPIE = old MIE
    cpu.mstatus |= MSTATUS_MPP;          // MPP = 11 (M-mode)

    // 3. 跳转到 mtvec
    // mtvec 模式：bit[1:0]
    //   00 = Direct：直接跳到 mtvec
    //   01 = Vectored：异常直接跳，中断跳到 mtvec + cause*4
    uint32_t mode = cpu.mtvec & 3;
    uint32_t base = cpu.mtvec & ~3u;
    if (mode == 1 && (cause >> 31)) {
        // Vectored 模式下的中断
        cpu.pc = base + (cause & 0x7fffffffu) * 4;
    } else {
        cpu.pc = base;
    }
}
```

### 3.2 do_mret()

```cpp
static void do_mret() {
    // 1. 恢复 PC
    cpu.pc = cpu.mepc;
    // ecall 的 mepc 是 ecall 指令本身，需要加 4 跳过
    // （约定：B 的 trap handler 在返回前手动将 mepc += 4）

    // 2. 恢复 mstatus：MIE = MPIE，MPIE = 1，MPP = U-mode
    uint32_t mpie = (cpu.mstatus >> 7) & 1;
    cpu.mstatus &= ~(MSTATUS_MIE | MSTATUS_MPIE | MSTATUS_MPP);
    cpu.mstatus |= (mpie << 3);   // MIE = old MPIE
    cpu.mstatus |= MSTATUS_MPIE; // MPIE = 1
    // MPP = 0 (U-mode)
}
```

---

## 四、时钟中断

### 4.1 CLINT 模拟

CLINT（Core Local Interruptor）提供 `mtime` 和 `mtimecmp` 两个 MMIO 寄存器，当 `mtime >= mtimecmp` 时触发时钟中断。

在 `mmio.h` 中添加：

```cpp
#define CLINT_MTIME    0xa0000100u   // 64 位，分两个 32 位寄存器
#define CLINT_MTIMECMP 0xa0000108u
```

在内存 MMIO 分发中实现：

```cpp
// 模拟 mtime（与系统定时器同步）
static uint64_t get_mtime() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static uint64_t mtimecmp = UINT64_MAX;   // 初始禁用

// MMIO 读
if (addr == CLINT_MTIME)      return (uint32_t)(get_mtime() & 0xffffffff);
if (addr == CLINT_MTIME + 4)  return (uint32_t)(get_mtime() >> 32);
if (addr == CLINT_MTIMECMP)   return (uint32_t)(mtimecmp & 0xffffffff);
if (addr == CLINT_MTIMECMP+4) return (uint32_t)(mtimecmp >> 32);

// MMIO 写
if (addr == CLINT_MTIMECMP)   { mtimecmp = (mtimecmp & 0xffffffff00000000ull) | data; return; }
if (addr == CLINT_MTIMECMP+4) { mtimecmp = (mtimecmp & 0x00000000ffffffffull) | ((uint64_t)data << 32); return; }
```

### 4.2 中断检查

在 `cpu_exec` 的每次循环中，检查时钟中断：

```cpp
static void check_interrupt() {
    // 时钟中断：mtime >= mtimecmp 且 MIE && MTIE
    if ((cpu.mstatus & MSTATUS_MIE)
        && (cpu.mie & (1u << 7))         // MTIE（Machine Timer Interrupt Enable）
        && get_mtime() >= mtimecmp) {
        do_trap(CAUSE_MACHINE_TIMER_INT, 0);
    }
}

// 在 exec_once() 返回前调用
check_interrupt();
```

---

## 五、向 B 暴露的接口

以下内容写入 `score/include/trap.h`，B 可以直接引用这个头文件来了解 trap 机制：

```cpp
#pragma once
// SCore Trap 机制说明（供 SOS / SHAL 参考）
//
// 1. mtvec：B 通过 CSR 指令设置 trap 处理函数地址
//    写法：asm volatile("csrw mtvec, %0" : : "r"(handler_addr));
//
// 2. trap 发生时，SCore 硬件自动完成：
//    - mepc  ← 触发 trap 的指令地址
//    - mcause← 原因码（见 csr.h 中 CAUSE_* 定义）
//    - mtval ← 附加信息（地址等）
//    - mstatus 更新（MIE 关闭）
//    - PC   ← mtvec（Direct 模式）
//
// 3. 从 trap 返回：执行 mret 指令
//    - PC ← mepc（ecall 需提前 mepc += 4）
//    - mstatus 恢复
//
// 4. 上下文保存：SCore 不自动保存通用寄存器，
//    B 的 trap handler 必须用汇编在栈上保存/恢复所有寄存器
//
// 5. 时钟中断触发条件：
//    - mstatus.MIE = 1
//    - mie.MTIE（bit7）= 1
//    - CLINT mtime >= mtimecmp
```

---

## 六、独立验证

A 可以编写一个最简的 trap 测试：

```asm
# test_trap.S：测试 ecall 和 mret
.section .text
.globl _start
_start:
    la   t0, trap_handler
    csrw mtvec, t0           # 设置 trap 向量
    ecall                    # 触发 trap，期望跳到 trap_handler
    # 若 mret 正确，会回到这里
    ebreak                   # 测试结束

trap_handler:
    # 简单 handler：将 mcause 读到 a0 后 mret
    csrr a0, mcause          # a0 = CAUSE_ECALL_FROM_M（0xb）
    csrr t0, mepc
    addi t0, t0, 4           # mepc += 4（跳过 ecall 指令）
    csrw mepc, t0
    mret
```

```bash
riscv32-unknown-elf-gcc -march=rv32im -mabi=ilp32 -nostdlib \
    -T stratum-apps/cpu-tests/link.ld test_trap.S -o test_trap.elf
./score/score -e test_trap.elf
# 在 SDB 中：
# (sdb) si 10
# (sdb) info r     ← 查看 a0，期望 = 0xb（CAUSE_ECALL_FROM_M）
```

---

## 七、本阶段完成检查清单

- [ ] `csrw mtvec` 能设置 trap 向量地址
- [ ] `ecall` 正确触发 trap，PC 跳转到 mtvec
- [ ] `mret` 正确恢复 PC 和 mstatus
- [ ] `mcause`、`mepc` 在 trap 后值正确
- [ ] 时钟中断能定期触发（在 `check_interrupt()` 中验证）
- [ ] `get_csr()` 对未知 CSR 不崩溃
- [ ] `trap.h` 接口说明文件已提交，B 可以看

---

## 八、提交本阶段成果

```bash
git add score/
git commit -m "pa4-a: add RISC-V trap/interrupt mechanism and CSR support"
git push origin feat/pa4-trap
```

> 不要向 `dev` 合入，等成员 B 完成 PA4-B 后，进入 [联调4] 再合并。

---

*本阶段完成后，等待成员 B 完成 [PA4-B-SOS操作系统]，再进入 [联调4-OS集成]。*
