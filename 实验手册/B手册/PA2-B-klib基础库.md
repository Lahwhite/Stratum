# 实验手册 · PA2-B：klib 基础库

> **本手册使用者：成员 B**  
> **阶段目标：** 实现 klib（内核级 C 标准库子集），在 `native` 平台上通过测试，搭建 SHAL 框架与 `hello` 程序骨架，为联调2做好准备。  
> **预计时间：** 3～4 天  
> **前置条件：** 联调1 完成（tag `v1.0-pa1`）  
> **本阶段涉及文件（在 `shal/` 目录，独立于 `score/`）：**
>
> ```
> shal/
> ├── Makefile                ← 本阶段新建（SHAL 构建系统）
> ├── include/
> │   ├── klib.h              ← 本阶段新建（klib 对外接口）
> │   ├── klib-macros.h       ← 本阶段新建（工具宏）
> │   └── am.h                ← 本阶段新建（SHAL 抽象机器接口，空框架）
> ├── klib/
> │   ├── string.c            ← 本阶段新建
> │   ├── stdio.c             ← 本阶段新建
> │   ├── stdlib.c            ← 本阶段新建
> │   └── softdiv.c           ← 本阶段新建（软件乘除法，RV32I 无硬件乘除指令）
> ├── platform/
> │   └── native/
> │       └── ioe.c           ← 本阶段新建（native 平台 IO：直接调系统调用）
> └── apps/
>  └── hello/
>      ├── Makefile        ← 本阶段新建
>      └── main.c          ← 本阶段新建
> ```
> **本阶段不涉及：** `platform/riscv32/` — RISC-V 平台的 IOE 由 PA3 完成；`score/` 目录完全由成员 A 负责，本阶段不涉及。

---

## 实验一：搭建 SHAL 目录结构

### 目标
建立 SHAL（Stratum Hardware Abstraction Layer）的目录结构，理解"抽象机器"的设计思路。

### 背景知识

**为什么需要抽象机器？**

如果你直接在程序里调用 `printf`，这个程序依赖 Linux 的 `write` 系统调用。它无法运行在没有操作系统的裸机上（比如 SCore 模拟的 RISC-V 环境）。

抽象机器（AM）的思路是：定义一套与平台无关的接口（如 `putch(c)` 输出一个字符），然后针对不同平台提供不同实现：

- `native` 平台：调用 Linux 的 `write()` 系统调用
- `riscv32` 平台：向 SCore 模拟的 UART 硬件寄存器写入

上层程序（klib、操作系统、应用程序）只使用这套接口，与具体平台解耦。**同一份代码，更换"平台"就能在不同环境运行**。

**SHAL 的分层：**

```
应用程序（hello、游戏）
       ↓
  klib（printf / sprintf / memcpy ...）
       ↓
  SHAL 接口（putch / getkey / uptime ...）
       ↓
  平台实现（native / riscv32）
```

### 步骤

**1. 创建目录结构**

```bash
cd stratum/shal
mkdir -p include klib platform/native platform/riscv32 apps/hello
touch platform/riscv32/.gitkeep   # 先留空，PA3 填充
```

**2. 编写 `include/am.h`（空框架）**

现在只需要声明 `putch`，其他接口 PA3 阶段再加：

```c
#pragma once

// 输出一个字符（平台相关实现）
void putch(char c);
```

**3. 编写 `include/klib-macros.h`**

一些常用宏，避免在 klib 实现里写魔法数字：

```c
#pragma once

#define NULL    ((void *)0)
#define true    1
#define false   0
#define bool    _Bool

// 断言：条件不成立时打印错误并停止
#define assert(cond) \
    do { \
        if (!(cond)) { \
            puts("Assertion failed: " #cond); \
            while(1); \
        } \
    } while(0)
```

### 检查点
- [x] 目录结构正确创建
- [x] `am.h` 和 `klib-macros.h` 编译（被其他文件 include 时）不报错

---

## 实验二：实现 klib —— string 模块

### 目标
在 `shal/klib/string.c` 中实现一套不依赖任何系统头文件的字符串函数。

### 背景知识

**为什么不直接用 `<string.h>`？**  
`<string.h>` 中的函数最终依赖操作系统。在裸机环境中没有这些实现，只有硬件和你自己写的代码。因此我们需要从零实现这些函数。

这是一个极好的练习：你会发现这些"基础"函数并不复杂，理解它们的实现能帮助你在调试时更清楚地知道底层发生了什么。

**编写 `include/klib.h`，声明以下函数：**

```c
// string.h 子集
size_t strlen(const char *s);
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
char  *strcat(char *dst, const char *src);
int    strcmp(const char *s1, const char *s2);
int    strncmp(const char *s1, const char *s2, size_t n);
char  *strchr(const char *s, int c);

// string.h 的内存函数
void  *memset(void *s, int c, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    memcmp(const void *s1, const void *s2, size_t n);
```

### 步骤

**1. 逐函数实现**

在 `shal/klib/string.c` 中实现上面所有函数。每个函数的实现思路如下：

| 函数 | 核心思路 |
|------|---------|
| `strlen` | 从头遍历，找到 `\0`，返回偏移量 |
| `strcpy` | 逐字节复制直到遇到 `\0`（包括 `\0` 本身） |
| `strncpy` | 最多复制 n 字节；若源串不足 n 字节，剩余用 `\0` 填充 |
| `strcat` | 先 `strlen(dst)` 找到末尾，再从那里开始 `strcpy` |
| `strcmp` | 逐字节比较，返回第一个不同字节的差值 |
| `memset` | 把 `n` 个字节都设为 `(unsigned char)c` |
| `memcpy` | 从低地址到高地址逐字节复制（不处理重叠） |
| `memmove` | 先检查是否重叠：若 `dst < src`，正向复制；若 `dst > src`，从高地址向低地址复制 |

**2. 注意事项**

- 所有函数的参数/返回值类型与标准库完全一致（这样应用代码不需要修改就能用）
- `size_t` 是无符号类型，注意 `n = 0` 时的边界情况（不应该有任何操作）
- `memmove` 的重叠处理是核心难点，画图帮助理解

**3. 编写独立测试**

在 `shal/klib/` 中写一个 `test_string.c`，用 `assert` 验证每个函数：

```c
#include <assert.h>  // 只在 native 测试时使用系统版 assert
#include <string.h>  // 只在 native 测试时与标准库对比

// 先用我们自己的实现
size_t my_strlen(const char *s);
// ...

void test_strlen() {
    assert(my_strlen("hello") == 5);
    assert(my_strlen("") == 0);
    assert(my_strlen("ab\0cd") == 2);
    printf("strlen: PASS\n");
}
```

### 检查点
- [x] 每个函数在 native 上编译并通过测试
- [x] `memmove` 在 src 和 dst 重叠情况下行为正确（画图验证）

---

## 实验三：实现 klib —— stdio 模块

### 目标
实现 `sprintf`、`printf`（通过 `putch`）、`puts`，这是上层程序最常用的输出函数。

### 背景知识

**`printf` 的实现思路：**  
`printf` 最难的部分是格式化字符串解析。但我们只需要支持常用的格式说明符：`%d`、`%u`、`%x`、`%s`、`%c`，已经足够运行操作系统了。

实现路线：先实现 `sprintf`（输出到字符串缓冲区），再让 `printf` 调用 `sprintf`，把结果逐字节 `putch` 输出。

**`va_list` 的使用：**  
格式化函数接受可变参数，需要用 `#include <stdarg.h>` 中的 `va_start`、`va_arg`、`va_end` 宏。

### 步骤

**1. 实现 `sprintf` 的核心：整数转字符串**

这是 `printf` 的难点。以十进制为例，`123` → `"123"` 的转换：

```
步骤1：用 % 运算逐位取出数字：123%10=3，12%10=2，1%10=1
步骤2：数字是倒序的，需要翻转
步骤3：负数：先处理符号，再取绝对值
```

提示：可以先写一个辅助函数 `int2str(int val, char *buf, int base)`，处理十进制（base=10）和十六进制（base=16）的统一逻辑。

**2. 实现 `vsprintf(char *buf, const char *fmt, va_list ap)`**

遍历格式字符串：
- 遇到普通字符：直接复制到 buf
- 遇到 `%`：读下一个字符判断格式说明符，从 `ap` 中取对应参数，转换后写入 buf
- 支持 `%d`（有符号十进制）、`%u`（无符号）、`%x`（十六进制小写）、`%s`（字符串）、`%c`（字符）、`%%`（输出 `%`）

**3. 实现 `sprintf` 和 `printf`**

```c
int sprintf(char *out, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsprintf(out, fmt, ap);
    va_end(ap);
    return ret;
}

int printf(const char *fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    int ret = vsprintf(buf, fmt, ap);
    va_end(ap);
    for (int i = 0; buf[i]; i++) putch(buf[i]);
    return ret;
}
```

**4. 在 native 平台实现 `putch`**

```c
// shal/platform/native/ioe.c
#include <unistd.h>  // write()

void putch(char c) {
    write(1, &c, 1);   // 写到 stdout（fd=1）
}
```

### 检查点
- [ ] `sprintf(buf, "%d", -42)` 结果为 `"-42"`
- [ ] `sprintf(buf, "0x%x", 255)` 结果为 `"0xff"`
- [ ] `printf("hello %s %d\n", "world", 42)` 输出正确

---

## 实验四：实现 klib —— stdlib 模块

### 目标
实现 `atoi`、`itoa`（可选）、`malloc`/`free` 的最简版本。

### 步骤

**1. 实现 `atoi`**

将字符串 `"123"` 转为整数 123：跳过前导空白 → 处理符号 → 逐字符累加。

**2. 实现简单内存分配器**

用一个静态数组模拟堆（SCore 没有真正的堆）：

```c
static uint8_t heap[4 * 1024 * 1024];  // 4MB 静态堆
static size_t  heap_used = 0;

void *malloc(size_t size) {
    // 对齐到 8 字节
    size = (size + 7) & ~7u;
    if (heap_used + size > sizeof(heap)) return NULL;
    void *p = heap + heap_used;
    heap_used += size;
    return p;
}

void free(void *p) {
    // 极简版本：不释放，只用于不需要真正回收的场景
    (void)p;
}
```

> 这个 `free` 是空操作。对于我们的操作系统而言，内核通常不需要频繁释放内存，这个简单实现够用了。

### 检查点
- [ ] `atoi("-123")` 返回 `-123`
- [ ] `malloc(100)` 返回非 NULL，且连续调用不返回重叠地址
- [ ] `malloc(4*1024*1024 + 1)` 返回 NULL

---

## 实验四补充：实现 klib —— softdiv 模块（RV32I 专用）

### 目标
实现软件乘除法函数，供 RISC-V rv32i 架构使用（rv32i 没有硬件乘除法指令）。

### 背景知识

**为什么需要软件乘除法？**

RISC-V 的基础整数指令集 `rv32i` 只包含加减、逻辑运算、移位和分支指令，**没有乘法和除法指令**。当 GCC 编译代码遇到乘除法运算时，会生成对以下函数的调用：

| 函数名 | 功能 | 调用场景 |
|--------|------|----------|
| `__mulsi3` | 有符号整数乘法 | `a * b` |
| `__umulsi3` | 无符号整数乘法 | `(unsigned)a * (unsigned)b` |
| `__divsi3` | 有符号整数除法 | `a / b` |
| `__udivsi3` | 无符号整数除法 | `(unsigned)a / (unsigned)b` |
| `__modsi3` | 有符号整数取模 | `a % b` |
| `__umodsi3` | 无符号整数取模 | `(unsigned)a % (unsigned)b` |

这些函数是 GCC 的内部 ABI，必须提供实现才能在 rv32i 上链接通过。

### 步骤

**1. 实现 `shal/klib/softdiv.c`**

```c
// 有符号整数乘法
int __mulsi3(int a, int b) {
    int result = 0;
    int sign = 1;
    if (a < 0) { a = -a; sign = -sign; }
    if (b < 0) { b = -b; sign = -sign; }
    while (b > 0) {
        if (b & 1) result += a;
        a <<= 1;
        b >>= 1;
    }
    return sign * result;
}

// 无符号整数乘法
unsigned int __umulsi3(unsigned int a, unsigned int b) {
    unsigned int result = 0;
    while (b > 0) {
        if (b & 1) result += a;
        a <<= 1;
        b >>= 1;
    }
    return result;
}

// 有符号整数除法
int __divsi3(int a, int b) {
    int result = 0;
    int sign = 1;
    if (a < 0) { a = -a; sign = -sign; }
    if (b < 0) { b = -b; sign = -sign; }
    while (a >= b) {
        a -= b;
        result++;
    }
    return sign * result;
}

// 无符号整数除法
unsigned int __udivsi3(unsigned int a, unsigned int b) {
    unsigned int result = 0;
    while (a >= b) {
        a -= b;
        result++;
    }
    return result;
}

// 有符号整数取模
int __modsi3(int a, int b) {
    int sign = 1;
    if (a < 0) { a = -a; sign = -sign; }
    if (b < 0) b = -b;
    while (a >= b) a -= b;
    return sign * a;
}

// 无符号整数取模
unsigned int __umodsi3(unsigned int a, unsigned int b) {
    while (a >= b) a -= b;
    return a;
}
```

**2. 算法说明**

- **乘法**：使用移位-累加算法，将乘法分解为多次加法和移位
- **除法/取模**：使用减法迭代，从被除数中不断减去除数，计数即为商

> **优化提示**：以上是最简实现，效率不高。PA3 阶段可以用更高效的算法（如二分查找除法）替换。

### 检查点
- [ ] 所有函数在 native 平台编译通过
- [ ] `__divsi3(10, 3)` 返回 `3`，`__modsi3(10, 3)` 返回 `1`
- [ ] `__divsi3(-10, 3)` 返回 `-3`，`__modsi3(-10, 3)` 返回 `-1`

---

## 实验五：hello 程序骨架

### 目标
编写第一个 SHAL 应用程序 `hello`，在 native 平台运行，打印 "Hello, Stratum!" 并退出。

### 步骤

**1. 编写 `shal/apps/hello/main.c`**

```c
#include <klib.h>

int main() {
    printf("Hello, Stratum!\n");
    printf("This program runs on the Abstract Machine.\n");
    printf("1 + 1 = %d\n", 1 + 1);
    return 0;
}
```

**2. 编写 `shal/apps/hello/Makefile`**

针对 `native` 平台：

```makefile
CC     := gcc
CFLAGS := -std=c99 -Wall -I../../include

SRCS := main.c \
        ../../klib/string.c \
        ../../klib/stdio.c  \
        ../../klib/stdlib.c \
        ../../platform/native/ioe.c

hello: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f hello
```

**3. 编译运行（native 平台）**

```bash
cd shal/apps/hello
make hello
./hello
```

期望输出：
```
Hello, Stratum!
This program runs on the Abstract Machine.
1 + 1 = 2
```

**4. 扩展 Makefile 支持 RISC-V 平台**

在 Makefile 中添加 RISC-V 交叉编译规则：

```makefile
# RISC-V 平台编译规则
RISCV_CC     := riscv64-unknown-elf-gcc
RISCV_CFLAGS := -march=rv32i -mabi=ilp32 -std=c99 -nostdlib -Wall -I../../include \
                -Wl,-Ttext=0x80000000 -Wl,-N -e main
RISCV_SRCS   := main.c \
                ../../klib/string.c \
                ../../klib/stdio.c  \
                ../../klib/stdlib.c \
                ../../klib/softdiv.c \
                ../../platform/riscv32/ioe.c

hello.elf: $(RISCV_SRCS)
	$(RISCV_CC) $(RISCV_CFLAGS) -o $@ $^

clean:
	rm -f hello hello.elf
```

> **参数说明：**
> - `-march=rv32i`：目标架构为 RISC-V 32 位基础整数指令集
> - `-mabi=ilp32`：使用 32 位整数 ABI（int/long/pointer 都是 32 位）
> - `-nostdlib`：不链接标准库（裸机环境）
> - `-Wl,-Ttext=0x80000000`：指定代码段起始地址为 `0x80000000`（SCore 默认加载地址）
> - `-Wl,-N`：禁用数据段只读
> - `-e main`：指定入口点为 `main`

**5. 编译 RISC-V 版本**

```bash
cd shal/apps/hello
make hello.elf
```

编译成功后会生成 `hello.elf` 文件，这是一个 RISC-V 32 位 ELF 可执行文件，可以在 SCore 模拟器中运行。

**6. 验证 ELF 文件格式**

```bash
file hello.elf
# 期望输出：hello.elf: ELF 32-bit LSB executable, UCB RISC-V, RVC, soft-float ABI, version 1 (SYSV), statically linked, not stripped
```

**7. 为联调2做好准备：思考 RISC-V 平台需要什么**

`native` 平台的 `putch` 调用了 Linux 的 `write()`，但 RISC-V 平台没有 Linux。告诉 A：联调2时，你需要知道 SCore 的 UART 地址是多少（这样你就能实现 `riscv32` 平台的 `putch`，向那个地址写一个字节）。

### 检查点
- [x] `./hello` 输出正确
- [x] 没有 warning
- [x] 代码不依赖任何系统头文件（只有 `platform/native/ioe.c` 可以 `#include <unistd.h>`，其余文件不能用系统头文件）
- [x] `make hello.elf` 编译成功，生成 `hello.elf`
- [x] `file hello.elf` 显示为 `ELF 32-bit LSB executable, UCB RISC-V`

---

## 阶段总结

完成本阶段后，你有了：

| 文件 | 内容 |
|------|------|
| `shal/klib/string.c` | memcpy / strcpy / strcmp 等 |
| `shal/klib/stdio.c` | printf / sprintf（通过 putch） |
| `shal/klib/stdlib.c` | atoi / malloc |
| `shal/klib/softdiv.c` | 软件乘除法（RV32I 专用） |
| `shal/platform/native/ioe.c` | native 平台 putch |
| `shal/apps/hello/` | 第一个 SHAL 程序（native 和 RISC-V 双版本） |

**你现在能回答：**
- 为什么不能直接用 `<string.h>`？抽象机器解决了什么问题？
- `printf` 是如何处理 `%d` 格式符的？整数如何转为字符串？
- `memmove` 和 `memcpy` 的区别是什么？什么情况下必须用 `memmove`？
- 为什么这个极简的 `malloc` 没有 `free`，但对于操作系统内核够用？
- 为什么 RV32I 需要软件乘除法？GCC 遇到乘除法时会生成什么函数调用？

---

## 进入下一步的前提

满足以下所有条件后，等待成员 A 完成 PA2-A，再一起进入「联调2」：

- [ ] 所有检查点通过
- [ ] `hello` 在 native 平台正常运行
- [ ] 代码已推送到 `feat/pa2-klib` 分支

---

## 常见问题

**Q：`printf("%d", 0)` 输出空字符串或乱码？**  
A：整数转字符串时，`0` 是边界情况。检查你的循环条件：如果用 `while (val > 0)`，`val == 0` 时不会执行任何循环，结果是空字符串。正确处理方式：先处理 `val == 0` 的特殊情况，或者用 `do { } while`。

**Q：`memmove` 在重叠情况下结果不对？**  
A：关键是复制方向。当 `dst > src` 且有重叠时，从前往后复制会覆盖还没读取的源数据。必须从后往前复制（从 `src+n-1` 开始，到 `src` 结束）。

**Q：`hello` 链接时报 `undefined reference to putchar`？**  
A：检查 Makefile 的 SRCS 列表，确保 `platform/native/ioe.c` 在其中。另外检查 `printf` 的实现是否真的调用了 `putch` 而不是 `putchar`。

**Q：编译 `hello.elf` 时报 `undefined reference to __divsi3/__modsi3`？**  
A：RV32I 没有硬件乘除法指令，需要提供软件实现。检查 Makefile 的 `RISCV_SRCS` 是否包含 `../../klib/softdiv.c`。

**Q：`make hello.elf` 时报 `No rule to make target '../../klib/softdiv.c'`？**  
A：`softdiv.c` 文件尚未创建。按照「实验四补充」中的步骤创建该文件。

**Q：`make hello.elf` 时报 `missing separator`？**  
A：Makefile 的命令行必须以 TAB 字符开头，不能用空格。检查 `hello.elf:` 目标下的命令是否使用了真正的 TAB。

**Q：`file hello.elf` 显示 `ELF 64-bit` 而不是 `ELF 32-bit`？**  
A：检查 RISCV_CFLAGS 是否包含 `-march=rv32i -mabi=ilp32`。缺少这些参数会导致编译器默认生成 64 位代码。
