# PA4-B — SOS 操作系统
> 成员 B 独立完成 · 前置：联调3 完成（v3.0-pa3 已打 tag）· 预计工时：6～8 天

---

## 本手册范围说明

本手册仅包含**成员 B 的工作**：

- Trap 入口汇编（保存/恢复上下文，调用 C 语言 handler）
- 系统调用分发框架
- 基础系统调用：`SYS_write`、`SYS_read`、`SYS_exit`、`SYS_brk`
- Ramdisk 文件系统
- ELF 用户程序加载器
- 进程管理与上下文切换
- 基于时钟中断的轮转调度（Round-Robin）

**前置可用成果（来自联调3，已稳定）：**
- 完整 SHAL IOE 接口（UART、定时器、键盘、VGA 均可用）
- klib 完整（printf、string、malloc 均可用）
- 上述成果可直接在 native 平台运行，也可在 SCore 上运行（已联调）

**来自成员 A 的接口约定（见 `score/include/trap.h`）：**
- `mtvec` 写入方式：`asm volatile("csrw mtvec, %0" : : "r"(addr))`
- `mcause` 编码：`CAUSE_ECALL_FROM_M = 0xb`，`CAUSE_MACHINE_TIMER_INT = 0x80000007`
- ecall 返回前需将 `mepc += 4`（B 在 handler 中完成）
- 时钟中断触发需开启 `mstatus.MIE` 和 `mie.MTIE`，并写 CLINT `mtimecmp`

---

## 目标

**本阶段完成标志：**
- `hello` 用户程序能通过系统调用打印字符
- `SYS_exit` 能正常终止程序并返回内核
- 文件系统能挂载 ramdisk，`open/read/write/close` 工作正常
- 能从文件系统加载一个 ELF 用户程序并跳转执行
- 基于时钟中断的多进程轮转调度能稳定运行

---

## 一、目录结构

```bash
mkdir -p sos/src sos/include
```

```
sos/
├── include/
│   ├── sos.h         # 全局类型、宏
│   ├── proc.h        # 进程控制块
│   ├── fs.h          # 文件系统接口
│   └── syscall.h     # 系统调用编号
└── src/
    ├── trap.S        # Trap 入口汇编
    ├── trap.cpp      # Trap 分发 C 代码
    ├── syscall.cpp   # 系统调用实现
    ├── fs.cpp        # 文件系统
    ├── loader.cpp    # ELF 加载器
    ├── proc.cpp      # 进程管理
    └── main.cpp      # OS 入口
```

---

## 二、系统调用编号表

创建 `sos/include/syscall.h`（**双方共用，也提交到 `score/include/` 供 A 查阅**）：

```cpp
#pragma once

// Stratum 系统调用编号
// 用户程序通过 ecall 触发，a7 寄存器存放调用号
#define SYS_exit   1
#define SYS_write  2
#define SYS_read   3
#define SYS_open   4
#define SYS_close  5
#define SYS_lseek  6
#define SYS_brk    7
#define SYS_execve 8
```

---

## 三、Trap 入口汇编

RISC-V 不自动保存通用寄存器，trap handler 必须用汇编手动保存。

创建 `sos/src/trap.S`：

```asm
#define CONTEXT_SIZE (32 * 4 + 4)   # 32 个 GPR + mepc，每个 4 字节

.section .text
.globl __am_asm_trap
__am_asm_trap:
    # 在内核栈上分配上下文空间
    addi sp, sp, -CONTEXT_SIZE

    # 保存所有通用寄存器（x1~x31；x0 恒为 0 不保存）
    sw   x1,   4(sp)
    sw   x2,   8(sp)    # 注意：sp 在修改前已调整，这里保存的是调整后的值
    sw   x3,  12(sp)
    sw   x4,  16(sp)
    sw   x5,  20(sp)
    sw   x6,  24(sp)
    sw   x7,  28(sp)
    sw   x8,  32(sp)
    sw   x9,  36(sp)
    sw   x10, 40(sp)
    sw   x11, 44(sp)
    sw   x12, 48(sp)
    sw   x13, 52(sp)
    sw   x14, 56(sp)
    sw   x15, 60(sp)
    sw   x16, 64(sp)
    sw   x17, 68(sp)
    sw   x18, 72(sp)
    sw   x19, 76(sp)
    sw   x20, 80(sp)
    sw   x21, 84(sp)
    sw   x22, 88(sp)
    sw   x23, 92(sp)
    sw   x24, 96(sp)
    sw   x25, 100(sp)
    sw   x26, 104(sp)
    sw   x27, 108(sp)
    sw   x28, 112(sp)
    sw   x29, 116(sp)
    sw   x30, 120(sp)
    sw   x31, 124(sp)

    # 保存 mepc
    csrr t0, mepc
    sw   t0, 128(sp)

    # 调用 C 语言 trap 分发函数
    # 参数：sp（上下文指针）
    mv   a0, sp
    call __am_irq_handle

    # 从上下文恢复 mepc（可能已被调度修改）
    lw   t0, 128(sp)
    csrw mepc, t0

    # 恢复所有通用寄存器
    lw   x1,   4(sp)
    lw   x2,   8(sp)
    lw   x3,  12(sp)
    lw   x4,  16(sp)
    lw   x5,  20(sp)
    lw   x6,  24(sp)
    lw   x7,  28(sp)
    lw   x8,  32(sp)
    lw   x9,  36(sp)
    lw   x10, 40(sp)
    lw   x11, 44(sp)
    lw   x12, 48(sp)
    lw   x13, 52(sp)
    lw   x14, 56(sp)
    lw   x15, 60(sp)
    lw   x16, 64(sp)
    lw   x17, 68(sp)
    lw   x18, 72(sp)
    lw   x19, 76(sp)
    lw   x20, 80(sp)
    lw   x21, 84(sp)
    lw   x22, 88(sp)
    lw   x23, 92(sp)
    lw   x24, 96(sp)
    lw   x25, 100(sp)
    lw   x26, 104(sp)
    lw   x27, 108(sp)
    lw   x28, 112(sp)
    lw   x29, 116(sp)
    lw   x30, 120(sp)
    lw   x31, 124(sp)

    addi sp, sp, CONTEXT_SIZE
    mret
```

---

## 四、Trap 分发（C 语言部分）

创建 `sos/src/trap.cpp`：

```cpp
#include "sos.h"
#include "proc.h"
#include "syscall.h"
#include <cstdint>

// 上下文结构（与 trap.S 中的栈布局一一对应）
struct Context {
    uint32_t padding;     // offset 0（对齐）
    uint32_t gpr[31];     // x1~x31（offset 4~124）
    uint32_t mepc;        // offset 128
};

// 系统调用分发（在 syscall.cpp 中实现）
extern void do_syscall(Context *ctx);

// 时钟中断处理（在 proc.cpp 中实现）
extern void schedule();

// trap handler 入口，由 trap.S 调用
extern "C" void __am_irq_handle(Context *ctx) {
    uint32_t mcause;
    asm volatile("csrr %0, mcause" : "=r"(mcause));

    if (mcause == 0x80000007u) {
        // 时钟中断
        // 更新 mtimecmp，设置下一次中断时间（10ms 后）
        volatile uint32_t *mtimecmp = (volatile uint32_t*)0xa0000108u;
        uint64_t old_cmp = ((uint64_t)mtimecmp[1] << 32) | mtimecmp[0];
        uint64_t new_cmp = old_cmp + 10000;   // 10ms = 10000 us
        mtimecmp[1] = (uint32_t)(new_cmp >> 32);
        mtimecmp[0] = (uint32_t)(new_cmp & 0xffffffff);

        schedule();   // 切换进程
    } else if (mcause == 0x0000000bu) {
        // ecall（系统调用）
        ctx->mepc += 4;   // 返回时跳过 ecall 指令
        do_syscall(ctx);
    }
    // 其他异常暂时忽略或打印错误
}

// OS 初始化：设置 mtvec，开启中断
void trap_init() {
    // 设置 trap 向量（Direct 模式）
    uint32_t handler = (uint32_t)__am_asm_trap;
    asm volatile("csrw mtvec, %0" : : "r"(handler));

    // 设置第一次时钟中断
    volatile uint32_t *mtimecmp = (volatile uint32_t*)0xa0000108u;
    uint64_t first = 10000;   // 10ms 后第一次中断
    mtimecmp[1] = (uint32_t)(first >> 32);
    mtimecmp[0] = (uint32_t)(first & 0xffffffff);

    // 开启 Machine Timer Interrupt Enable
    asm volatile("csrs mie, %0" : : "r"(1u << 7));
    // 开启全局中断
    asm volatile("csrs mstatus, %0" : : "r"(1u << 3));
}
```

---

## 五、系统调用实现

创建 `sos/src/syscall.cpp`：

```cpp
#include "sos.h"
#include "proc.h"
#include "fs.h"
#include "syscall.h"
#include <cstdint>
#include <cstring>

struct Context { uint32_t padding; uint32_t gpr[31]; uint32_t mepc; };

// 寄存器别名（RISC-V ABI）
#define A0(ctx)  ((ctx)->gpr[10-1])   // a0 = x10（gpr 从 x1 开始，下标 9）
#define A1(ctx)  ((ctx)->gpr[11-1])
#define A2(ctx)  ((ctx)->gpr[12-1])
#define A7(ctx)  ((ctx)->gpr[17-1])   // 系统调用号

void do_syscall(Context *ctx) {
    uint32_t nr   = A7(ctx);
    uint32_t arg0 = A0(ctx);
    uint32_t arg1 = A1(ctx);
    uint32_t arg2 = A2(ctx);
    uint32_t ret  = 0;

    switch (nr) {
        case SYS_write: {
            // write(fd, buf, count)
            int    fd  = (int)arg0;
            const char *buf = (const char*)arg1;
            size_t count    = (size_t)arg2;
            if (fd == 1 || fd == 2) {
                // stdout / stderr → 直接通过 UART 输出
                for (size_t i = 0; i < count; i++) {
                    *(volatile uint8_t*)0xa0000000u = buf[i];
                }
                ret = count;
            } else {
                ret = fs_write(fd, buf, count);
            }
            break;
        }

        case SYS_read: {
            // read(fd, buf, count)
            int  fd  = (int)arg0;
            char *buf = (char*)arg1;
            size_t count = (size_t)arg2;
            ret = (fd == 0) ? 0 : fs_read(fd, buf, count);
            break;
        }

        case SYS_open: {
            const char *path = (const char*)arg0;
            int flags = (int)arg1;
            ret = (uint32_t)fs_open(path, flags);
            break;
        }

        case SYS_close:
            ret = (uint32_t)fs_close((int)arg0);
            break;

        case SYS_lseek:
            ret = (uint32_t)fs_lseek((int)arg0, (int32_t)arg1, (int)arg2);
            break;

        case SYS_brk:
            // 简单实现：直接返回 0（成功）
            // 真实实现需要管理堆内存上限
            ret = 0;
            break;

        case SYS_exit:
            // 终止当前进程
            proc_exit((int)arg0);
            // proc_exit 不返回（会切换到下一个进程）
            return;

        default:
            // 未知系统调用
            ret = (uint32_t)-1;
            break;
    }

    A0(ctx) = ret;   // 返回值放入 a0
}
```

---

## 六、文件系统（Ramdisk）

创建 `sos/include/fs.h`：

```cpp
#pragma once
#include <cstddef>

// 初始化文件系统（挂载 ramdisk）
void fs_init();

int    fs_open (const char *path, int flags);
int    fs_close(int fd);
size_t fs_read (int fd, void *buf, size_t count);
size_t fs_write(int fd, const void *buf, size_t count);
int    fs_lseek(int fd, int offset, int whence);
```

创建 `sos/src/fs.cpp`（静态文件表实现）：

```cpp
#include "fs.h"
#include <cstring>
#include <cstdlib>

// Ramdisk 镜像（由构建系统将用户程序打包进来）
extern uint8_t _ramdisk_start[];
extern uint8_t _ramdisk_end[];

// 文件条目（静态定义在链接时填入）
struct FileEntry {
    const char *name;
    uint32_t    offset;   // 在 ramdisk 中的偏移
    uint32_t    size;
};

// 文件表（由构建脚本自动生成 ramdisk_files.h）
#include "ramdisk_files.h"   // 包含 FileEntry file_table[]

static const int NR_FILES = sizeof(file_table) / sizeof(file_table[0]);

// 文件描述符表
struct FD {
    bool    in_use;
    int     file_idx;   // 指向 file_table
    int     offset;     // 当前读写位置
};

static const int NR_FD = 16;
static FD fd_table[NR_FD];

void fs_init() {
    memset(fd_table, 0, sizeof(fd_table));
    // fd 0/1/2 预留给 stdin/stdout/stderr
    fd_table[0].in_use = true;
    fd_table[1].in_use = true;
    fd_table[2].in_use = true;
}

int fs_open(const char *path, int flags) {
    // 查找文件
    int file_idx = -1;
    for (int i = 0; i < NR_FILES; i++) {
        if (strcmp(file_table[i].name, path) == 0) {
            file_idx = i; break;
        }
    }
    if (file_idx < 0) return -1;

    // 分配 fd
    for (int fd = 3; fd < NR_FD; fd++) {
        if (!fd_table[fd].in_use) {
            fd_table[fd] = {true, file_idx, 0};
            return fd;
        }
    }
    return -1;   // fd 耗尽
}

int fs_close(int fd) {
    if (fd < 3 || fd >= NR_FD || !fd_table[fd].in_use) return -1;
    fd_table[fd].in_use = false;
    return 0;
}

size_t fs_read(int fd, void *buf, size_t count) {
    if (fd < 3 || fd >= NR_FD || !fd_table[fd].in_use) return 0;
    FD &f = fd_table[fd];
    const FileEntry &e = file_table[f.file_idx];
    size_t remaining = e.size - f.offset;
    if (count > remaining) count = remaining;
    memcpy(buf, _ramdisk_start + e.offset + f.offset, count);
    f.offset += count;
    return count;
}

size_t fs_write(int fd, const void *buf, size_t count) {
    if (fd < 3 || fd >= NR_FD || !fd_table[fd].in_use) return 0;
    // ramdisk 为只读，写操作忽略
    return count;
}

int fs_lseek(int fd, int offset, int whence) {
    if (fd < 3 || fd >= NR_FD || !fd_table[fd].in_use) return -1;
    FD &f = fd_table[fd];
    const FileEntry &e = file_table[f.file_idx];
    int new_offset;
    switch (whence) {
        case 0: new_offset = offset;               break;  // SEEK_SET
        case 1: new_offset = f.offset + offset;    break;  // SEEK_CUR
        case 2: new_offset = e.size + offset;      break;  // SEEK_END
        default: return -1;
    }
    if (new_offset < 0 || (uint32_t)new_offset > e.size) return -1;
    f.offset = new_offset;
    return new_offset;
}
```

---

## 七、ELF 加载器与进程管理

创建 `sos/include/proc.h`：

```cpp
#pragma once
#include <cstdint>

#define NR_PROCS   4
#define STACK_SIZE (4 * 1024)  // 每进程 4KB 内核栈

struct PCB {
    uint32_t context[33];   // 32 GPR + mepc（调度时保存/恢复）
    uint32_t kstack[STACK_SIZE / 4];
    bool     running;
    int      pid;
};

extern PCB pcb_pool[NR_PROCS];
extern PCB *current;

void proc_init();
void proc_exit(int code);
void schedule();            // 时钟中断时调用，切换到下一个进程
int  proc_create(const char *elf_path);  // 从文件系统加载 ELF 并创建进程
```

创建 `sos/src/proc.cpp`：

```cpp
#include "proc.h"
#include "fs.h"
#include <cstring>
#include <cstdio>
#include <elf.h>

PCB  pcb_pool[NR_PROCS] = {};
PCB *current = nullptr;

void proc_init() {
    memset(pcb_pool, 0, sizeof(pcb_pool));
    for (int i = 0; i < NR_PROCS; i++) pcb_pool[i].pid = i;
}

void proc_exit(int code) {
    printf("[SOS] 进程 %d 退出，返回值 %d\n", current->pid, code);
    current->running = false;
    schedule();
    // schedule 不返回（会切换到其他进程）
    while (1);
}

// Round-Robin 调度
void schedule() {
    // 找到下一个 running 的 PCB
    int next = (current ? current->pid + 1 : 0) % NR_PROCS;
    for (int i = 0; i < NR_PROCS; i++) {
        if (pcb_pool[(next + i) % NR_PROCS].running) {
            PCB *prev = current;
            current = &pcb_pool[(next + i) % NR_PROCS];
            if (prev && prev != current) {
                // 保存 prev 的上下文（由 trap.S 的栈指针指向）
                // 恢复 current 的上下文
                // 简化：通过修改 mepc 实现（详细切换在联调时完善）
            }
            return;
        }
    }
    // 所有进程结束
    printf("[SOS] 所有进程结束，系统停机\n");
    asm volatile("ebreak");
}

// 从文件系统加载 ELF 并创建新进程
int proc_create(const char *elf_path) {
    int fd = fs_open(elf_path, 0);
    if (fd < 0) { printf("[SOS] 找不到文件: %s\n", elf_path); return -1; }

    // 读取 ELF header
    Elf32_Ehdr ehdr;
    fs_read(fd, &ehdr, sizeof(ehdr));
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        fs_close(fd); return -1;
    }

    // 加载各 PT_LOAD 段
    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf32_Phdr phdr;
        fs_lseek(fd, ehdr.e_phoff + i * ehdr.e_phentsize, 0);
        fs_read(fd, &phdr, sizeof(phdr));
        if (phdr.p_type != PT_LOAD) continue;
        fs_lseek(fd, phdr.p_offset, 0);
        fs_read(fd, (void*)(uintptr_t)phdr.p_paddr, phdr.p_filesz);
        if (phdr.p_memsz > phdr.p_filesz)
            memset((void*)(uintptr_t)(phdr.p_paddr + phdr.p_filesz),
                   0, phdr.p_memsz - phdr.p_filesz);
    }
    fs_close(fd);

    // 找空闲 PCB
    for (int i = 0; i < NR_PROCS; i++) {
        if (!pcb_pool[i].running) {
            PCB &p = pcb_pool[i];
            memset(&p.context, 0, sizeof(p.context));
            p.context[32] = ehdr.e_entry;   // mepc = 入口地址
            // sp = 进程内核栈顶
            p.context[2-1] = (uint32_t)(uintptr_t)(p.kstack + STACK_SIZE/4);
            p.running = true;
            printf("[SOS] 创建进程 %d：%s 入口 0x%08x\n",
                   i, elf_path, ehdr.e_entry);
            return i;
        }
    }
    return -1;  // 进程数已满
}
```

---

## 八、OS 主入口

创建 `sos/src/main.cpp`：

```cpp
#include "proc.h"
#include "fs.h"

extern void trap_init();

// OS 内核入口（由 SHAL 的 _start 跳转到此）
int main() {
    // 1. 初始化文件系统
    fs_init();

    // 2. 初始化进程管理
    proc_init();

    // 3. 设置 trap 向量，开启中断
    trap_init();

    // 4. 加载并运行第一个用户程序
    proc_create("/bin/hello");

    // 5. 切换到第一个进程（此后由时钟中断驱动调度）
    current = nullptr;
    schedule();

    // 不应到达此处
    return 0;
}
```

---

## 九、native 平台独立测试

B 可以在 native 平台验证文件系统和系统调用逻辑，不依赖 SCore：

```cpp
// sos/src/test_fs.cpp（仅 native 测试用）
#include "fs.h"
#include <cstdio>

// 构造一个假 ramdisk（native 测试用）
static uint8_t fake_ramdisk[] = "Hello from ramdisk!\n";
uint8_t *_ramdisk_start = fake_ramdisk;
uint8_t *_ramdisk_end   = fake_ramdisk + sizeof(fake_ramdisk);

// 假 file_table（通常由构建脚本生成）
FileEntry file_table[] = {
    {"/bin/hello", 0, sizeof(fake_ramdisk)},
};

int main() {
    fs_init();
    int fd = fs_open("/bin/hello", 0);
    if (fd < 0) { printf("FAIL: open\n"); return 1; }

    char buf[64] = {};
    size_t n = fs_read(fd, buf, sizeof(buf));
    printf("read %zu bytes: %s\n", n, buf);
    fs_close(fd);

    printf("fs test PASS\n");
    return 0;
}
```

---

## 十、本阶段完成检查清单

- [ ] `trap.S` 编译通过（用 riscv32 工具链）
- [ ] `do_syscall` 能正确分发 `SYS_write`（打印字符）
- [ ] `SYS_exit` 触发 `proc_exit`，不崩溃
- [ ] 文件系统：`fs_open/read/close` native 测试通过
- [ ] ELF 加载器能读取并加载 PT_LOAD 段
- [ ] `proc_create` 能初始化一个 PCB，并设置正确的入口地址
- [ ] `schedule()` 在只有一个进程时不崩溃
- [ ] 系统调用编号表 `syscall.h` 已提交

---

## 十一、提交本阶段成果

```bash
git add sos/
git commit -m "pa4-b: implement SOS kernel: trap, syscall, fs, proc"
git push origin feat/pa4-os
```

> 不要向 `dev` 合入，等成员 A 完成 PA4-A 后，进入 [联调4] 再合并。

---

*本阶段完成后，等待成员 A 完成 [PA4-A-中断与陷入]，再进入 [联调4-OS集成]。*
