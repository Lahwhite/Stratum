# PA2-B — klib 基础库与 hal-tests 框架
> 成员 B 独立完成 · 前置：联调1 完成（v1.0-pa1 已打 tag）· 预计工时：4～5 天

---

## 本手册范围说明

本手册仅包含**成员 B 的工作**：

- klib：运行在裸机/SHAL 环境下的轻量 C 标准库（`string.h`、`stdio.h`、`stdlib.h` 子集）
- SHAL 目录骨架和 `native` 平台的最小实现
- hal-tests 测试框架：验证 klib 正确性
- AM_BOOT 入口：从 `_start` 跳转到 C `main`

**不包含**：SCore CPU 实现、UART/VGA/键盘外设——这些由成员 A 在 PA2-A 和 PA3-A 完成。

**前置可用成果（来自联调1）：**
- 可以在 `native` 平台直接 `make` 运行，完全不依赖 SCore

---

## 目标

**本阶段完成标志：**
- klib 实现通过 `hal-tests/klib-tests` 所有用例（在 native 平台）
- SHAL 目录结构建立，`ARCH=native make` 能编译并运行简单程序
- 为联调2 准备好一个可在 SCore 上运行的 `hello` ELF 程序

---

## 一、目录结构建立

```bash
mkdir -p shal/src/native
mkdir -p shal/src/riscv
mkdir -p shal/include/arch
mkdir -p shal/klib/src
mkdir -p shal/klib/include
mkdir -p stratum-apps/hal-tests/klib-tests
```

---

## 二、SHAL 接口头文件定义

这是 SHAL 层的核心公共接口，联调后成员 A 的外设实现也会依赖这些头文件。

### 2.1 am.h — 顶层接口

创建 `shal/include/am.h`：

```cpp
#pragma once
#include <cstdint>

// ─── 平台初始化 ─────────────────────────────────────────────────────────
void __am_init_platform();

// ─── IOE 设备读写抽象 ────────────────────────────────────────────────────
// 使用方式：io_read(AM_TIMER_UPTIME) / io_write(AM_GPU_FBDRAW, ...)
// 具体结构体在 amdev.h 中定义
bool ioe_init();
void ioe_read (uintptr_t reg, void *buf);
void ioe_write(uintptr_t reg, const void *buf);

#define io_read(reg)   ({ reg##_T __ret; ioe_read(reg, &__ret); __ret; })
#define io_write(reg, ...) do { reg##_T __val = { __VA_ARGS__ }; ioe_write(reg, &__val); } while(0)
```

### 2.2 amdev.h — 设备寄存器定义

创建 `shal/include/amdev.h`（PA3-B 会扩充，此处仅定义 PA2 阶段用到的最小子集）：

```cpp
#pragma once
#include <cstdint>

// 定时器
#define AM_TIMER_UPTIME  1
typedef struct { uint64_t us; } AM_TIMER_UPTIME_T;

// 串口（字符输出）
#define AM_UART_TX       10
typedef struct { char data; } AM_UART_TX_T;
```

---

## 三、klib 实现

klib 是运行在 SHAL 之上的轻量 C 库，不依赖操作系统，只依赖平台提供的 `putch()` 和 `paddr_read`。

### 3.1 string.h 实现

创建 `shal/klib/src/string.cpp`：

```cpp
#include <cstring>   // 只作为类型参考，实际不用系统 string.h

extern "C" {

size_t strlen(const char *s) {
    size_t n = 0;
    while (*s++) n++;
    return n;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (n && (*d++ = *src++)) n--;
    while (n--) *d++ = '\0';
    return dst;
}

char *strcat(char *dst, const char *src) {
    strcpy(dst + strlen(dst), src);
    return dst;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char*)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char*)dst;
    const unsigned char *s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char*)dst;
    const unsigned char *s = (const unsigned char*)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = (const unsigned char*)a;
    const unsigned char *q = (const unsigned char*)b;
    while (n--) {
        if (*p != *q) return *p - *q;
        p++; q++;
    }
    return 0;
}

} // extern "C"
```

### 3.2 stdio.h 实现（核心：printf）

创建 `shal/klib/src/stdio.cpp`：

```cpp
#include <cstdarg>
#include <cstdint>
#include "am.h"   // 使用 ioe_write(AM_UART_TX, ...) 输出字符

extern "C" {

// 底层字符输出，通过 SHAL 的 UART 接口
static void putch_am(char c) {
    io_write(AM_UART_TX, c);
}

// 格式化到缓冲区（简化版 sprintf）
static int vsnprintf_impl(char *buf, size_t size, const char *fmt, va_list ap) {
    char *p = buf;
    char *end = buf + size - 1;

    auto put = [&](char c) {
        if (p < end) *p++ = c;
    };

    auto put_str = [&](const char *s) {
        while (*s) put(*s++);
    };

    auto put_uint = [&](uint64_t val, int base, bool upper, int width, char fill) {
        const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
        char tmp[32];
        int  n = 0;
        if (val == 0) tmp[n++] = '0';
        while (val > 0) { tmp[n++] = digits[val % base]; val /= base; }
        while (n < width) tmp[n++] = fill;
        for (int i = n-1; i >= 0; i--) put(tmp[i]);
    };

    while (*fmt) {
        if (*fmt != '%') { put(*fmt++); continue; }
        fmt++;

        // 解析 flags、width
        char fill  = ' ';
        int  width = 0;
        if (*fmt == '0') { fill = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width*10 + (*fmt++ - '0');

        // 解析长度修饰符
        bool is_long = false;
        if (*fmt == 'l') { is_long = true; fmt++; }

        switch (*fmt++) {
            case 'd': {
                int64_t v = is_long ? va_arg(ap, long) : va_arg(ap, int);
                if (v < 0) { put('-'); v = -v; }
                put_uint((uint64_t)v, 10, false, width, fill);
                break;
            }
            case 'u': {
                uint64_t v = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
                put_uint(v, 10, false, width, fill);
                break;
            }
            case 'x': {
                uint64_t v = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
                put_uint(v, 16, false, width, fill);
                break;
            }
            case 'X': {
                uint64_t v = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
                put_uint(v, 16, true, width, fill);
                break;
            }
            case 'p': {
                put('0'); put('x');
                put_uint((uintptr_t)va_arg(ap, void*), 16, false, 8, '0');
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char*);
                put_str(s ? s : "(null)");
                break;
            }
            case 'c':
                put((char)va_arg(ap, int));
                break;
            case '%':
                put('%');
                break;
            default:
                put('?');
                break;
        }
    }
    *p = '\0';
    return (int)(p - buf);
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    return vsnprintf_impl(buf, size, fmt, ap);
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char *fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    for (int i = 0; i < n; i++) putch_am(buf[i]);
    return n;
}

} // extern "C"
```

### 3.3 stdlib.h 实现（最小子集）

创建 `shal/klib/src/stdlib.cpp`：

```cpp
#include <cstdlib>
#include <cstdint>

extern "C" {

// 简单的 bump allocator，PA4 前够用
static uint8_t  heap[1024 * 1024];   // 1 MB 堆
static uint32_t heap_top = 0;

void *malloc(size_t size) {
    size = (size + 7) & ~7u;   // 8 字节对齐
    if (heap_top + size > sizeof(heap)) return nullptr;
    void *ptr = heap + heap_top;
    heap_top += size;
    return ptr;
}

void free(void *ptr) {
    // bump allocator 不支持 free，忽略
    (void)ptr;
}

int abs(int x) { return x < 0 ? -x : x; }

long strtol(const char *str, char **end, int base) {
    while (*str == ' ') str++;
    long sign = 1;
    if (*str == '-') { sign = -1; str++; }
    if (base == 0) {
        if (*str == '0' && (str[1] == 'x' || str[1] == 'X')) { base = 16; str += 2; }
        else if (*str == '0') { base = 8; str++; }
        else base = 10;
    }
    long result = 0;
    while (*str) {
        int d;
        if (*str >= '0' && *str <= '9') d = *str - '0';
        else if (*str >= 'a' && *str <= 'z') d = *str - 'a' + 10;
        else if (*str >= 'A' && *str <= 'Z') d = *str - 'A' + 10;
        else break;
        if (d >= base) break;
        result = result * base + d;
        str++;
    }
    if (end) *end = (char*)str;
    return sign * result;
}

} // extern "C"
```

---

## 四、native 平台实现

在 native 平台，SHAL 的 IOE 接口直接调用 Linux 系统调用，绕过 SCore。

创建 `shal/src/native/ioe.cpp`：

```cpp
#include "am.h"
#include "amdev.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>

bool ioe_init() { return true; }

void ioe_read(uintptr_t reg, void *buf) {
    switch (reg) {
        case AM_TIMER_UPTIME: {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            ((AM_TIMER_UPTIME_T*)buf)->us =
                (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
            break;
        }
        default:
            break;
    }
}

void ioe_write(uintptr_t reg, const void *buf) {
    switch (reg) {
        case AM_UART_TX:
            putchar(((AM_UART_TX_T*)buf)->data);
            fflush(stdout);
            break;
        default:
            break;
    }
}
```

---

## 五、hal-tests 测试套件

创建 `stratum-apps/hal-tests/klib-tests/test_string.cpp`：

```cpp
#include <cstring>
#include <cstdio>
#include <cassert>

// 测试 klib 的 string 函数
// 在 native 平台编译时，直接用系统 string.h 作为参考对比

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL: %s\n", #cond); fail++; } \
    else pass++; \
} while(0)

int main() {
    int pass = 0, fail = 0;

    // strlen
    CHECK(strlen("hello") == 5);
    CHECK(strlen("")      == 0);

    // strcpy / strcmp
    char buf[64];
    strcpy(buf, "world");
    CHECK(strcmp(buf, "world") == 0);

    // memset / memcmp
    memset(buf, 0xAB, 4);
    unsigned char ref[4] = {0xAB, 0xAB, 0xAB, 0xAB};
    CHECK(memcmp(buf, ref, 4) == 0);

    // memcpy
    char src[] = "stratum";
    char dst[16] = {};
    memcpy(dst, src, 8);
    CHECK(strcmp(dst, "stratum") == 0);

    // memmove（overlap）
    char over[] = "abcdef";
    memmove(over + 2, over, 4);
    CHECK(memcmp(over, "ababcd", 6) == 0);

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
```

编译并运行（native 平台）：

```bash
cd stratum-apps/hal-tests/klib-tests
g++ -std=c++17 -Ishal/klib/include test_string.cpp \
    ../../../shal/klib/src/string.cpp -o test_string
./test_string
```

---

## 六、为联调2 准备 hello ELF

在 `stratum-apps/apps/hello/` 下创建最简裸机程序，用于在 SCore 上跑通：

创建 `stratum-apps/apps/hello/hello.S`：

```asm
.section .text
.globl _start
_start:
    # 将 'H' 写入 UART 输出寄存器（地址 0xa0000000，联调2时确认）
    li   t0, 0xa0000000
    li   t1, 'H'
    sb   t1, 0(t0)
    li   t1, 'i'
    sb   t1, 0(t0)
    li   t1, '\n'
    sb   t1, 0(t0)
    ebreak           # 停机
```

> 注意：UART 地址 `0xa0000000` 需要在联调2时与成员 A 确认。此处先用占位地址。

---

## 七、本阶段完成检查清单

- [ ] `shal/klib/` 编译通过（native 平台）
- [ ] `test_string` 所有用例 PASS
- [ ] `printf` 基本格式符（`%d %s %x %p %c`）输出正确
- [ ] `malloc` 能分配内存，不崩溃
- [ ] native 平台的 UART ioe_write 能在终端打印字符
- [ ] `hello.S` 汇编程序能用 `riscv32-unknown-elf-gcc` 编译为 ELF

---

## 八、提交本阶段成果

```bash
git add shal/ stratum-apps/
git commit -m "pa2-b: add klib, SHAL skeleton and native platform"
git push origin feat/pa2-shal
```

> 不要向 `dev` 合入，等成员 A 完成 PA2-A 后，进入 [联调2] 再合并。

---

*本阶段完成后，等待成员 A 完成 [PA2-A-CPU核心]，再进入 [联调2-首次跨层联调]。*
