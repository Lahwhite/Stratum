# PA3-A — SCore 外设寄存器
> 成员 A 独立完成 · 前置：联调2 完成（v2.0-pa2 已打 tag）· 预计工时：3～4 天

---

## 本手册范围说明

本手册仅包含**成员 A 的工作**：

- SCore 侧的外设 MMIO 寄存器模拟
  - 定时器（已在联调2中实现，本阶段完善）
  - 键盘输入（基于 SDL2 事件队列）
  - VGA 帧缓冲（基于 SDL2 窗口）
- SDL2 主事件循环集成
- 外设地址统一在 `mmio.h` 中管理

**不包含**：SHAL 层的 IOE 接口实现（`io_read/io_write` 函数）——这由成员 B 在 [PA3-B] 中完成。A 只负责"硬件一侧"的 MMIO 寄存器行为，B 负责"软件一侧"的驱动接口。

**前置可用成果（来自联调2）：**
- UART MMIO 已打通（`0xa0000000`）
- 已有 klib `printf` 可用于调试输出

---

## 目标

**本阶段完成标志：**
- SDL2 窗口能正常弹出（256×256 像素）
- 写 VGA 帧缓冲地址能使像素显示在窗口上
- 按下键盘按键后，键盘状态寄存器能正确反映按键码
- 定时器读取稳定，精度在 ±1ms 内

---

## 一、更新 MMIO 地址表

更新 `score/include/mmio.h`，补全所有 PA3 外设地址：

```cpp
#pragma once
#include <cstdint>

// ─── 串口 UART ───────────────────────────────────────────────────────────
#define UART_TX_ADDR     0xa0000000u   // 写：发送 1 字节到终端

// ─── 定时器 ──────────────────────────────────────────────────────────────
#define TIMER_LO_ADDR    0xa0000048u   // 读：系统时间低 32 位（微秒）
#define TIMER_HI_ADDR    0xa000004cu   // 读：系统时间高 32 位（微秒）

// ─── 键盘 ────────────────────────────────────────────────────────────────
#define KBD_ADDR         0xa0000060u   // 读：当前按键事件（AM_KEY_* 编码）

// ─── VGA 帧缓冲 ──────────────────────────────────────────────────────────
#define VGACTL_ADDR      0xa0000100u   // 读：高 16 位=宽度，低 16 位=高度
#define FB_ADDR          0xa1000000u   // 帧缓冲起始地址（ARGB8888 格式）
// 帧缓冲大小 = 宽 × 高 × 4 字节
// 默认分辨率：400 × 300
#define SCREEN_W         400
#define SCREEN_H         300
```

---

## 二、SDL2 窗口管理

创建 `score/src/device/display.cpp`：

```cpp
#include "mmio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <SDL2/SDL.h>

static SDL_Window   *window   = nullptr;
static SDL_Renderer *renderer = nullptr;
static SDL_Texture  *texture  = nullptr;

// 帧缓冲（宿主机侧，4字节/像素 ARGB8888）
static uint32_t framebuf[SCREEN_W * SCREEN_H];

void display_init() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init 失败: %s\n", SDL_GetError());
        exit(1);
    }
    window = SDL_CreateWindow(
        "Stratum Display",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H,
        SDL_WINDOW_SHOWN
    );
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    texture  = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_W, SCREEN_H
    );
    memset(framebuf, 0, sizeof(framebuf));
}

// 将帧缓冲中的一个矩形区域更新到屏幕
// x,y：左上角；w,h：宽高；pixels：ARGB8888 数据
void display_update(int x, int y, int w, int h) {
    // 简化实现：直接刷新整个屏幕
    SDL_UpdateTexture(texture, nullptr, framebuf, SCREEN_W * 4);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

// 将 MMIO 写入映射到帧缓冲
void display_mmio_write(uint32_t addr, int len, uint32_t data) {
    if (addr < FB_ADDR || addr >= FB_ADDR + SCREEN_W * SCREEN_H * 4u) return;
    uint32_t offset = addr - FB_ADDR;
    if (len == 4) {
        framebuf[offset / 4] = data;
    }
    // 每次写入后立即刷新（效率低但简单；可改为批量刷新）
    display_update(0, 0, SCREEN_W, SCREEN_H);
}

uint32_t display_mmio_read(uint32_t addr, int len) {
    if (addr == VGACTL_ADDR) {
        return ((uint32_t)SCREEN_W << 16) | (uint32_t)SCREEN_H;
    }
    if (addr >= FB_ADDR && addr < FB_ADDR + SCREEN_W * SCREEN_H * 4u) {
        return framebuf[(addr - FB_ADDR) / 4];
    }
    return 0;
}
```

---

## 三、键盘输入模块

创建 `score/src/device/keyboard.cpp`：

```cpp
#include "mmio.h"
#include <SDL2/SDL.h>
#include <cstdint>

// AM 按键码定义（与 SHAL 的 amdev.h 保持一致）
// 0 = 无事件，高位 bit31=1 表示按下，=0 表示松开，低 8 位 = 按键码
#define KEY_NONE    0u
#define KEY_DOWN    (1u << 31)

// SDL2 scancode → AM keycode 映射（只列出常用键）
static uint32_t sdl_to_am(SDL_Scancode sc) {
    // 字母键
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
        return (uint32_t)(sc - SDL_SCANCODE_A + 1);
    // 数字键
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0)
        return (uint32_t)(sc - SDL_SCANCODE_1 + 27);
    switch (sc) {
        case SDL_SCANCODE_ESCAPE:    return 40;
        case SDL_SCANCODE_RETURN:    return 41;
        case SDL_SCANCODE_SPACE:     return 42;
        case SDL_SCANCODE_LEFT:      return 80;
        case SDL_SCANCODE_RIGHT:     return 81;
        case SDL_SCANCODE_UP:        return 82;
        case SDL_SCANCODE_DOWN:      return 83;
        default:                     return 0;
    }
}

// 按键事件队列（环形缓冲）
static const int KBD_BUF_SIZE = 64;
static uint32_t  kbd_buf[KBD_BUF_SIZE];
static int       kbd_head = 0, kbd_tail = 0;

static void kbd_push(uint32_t val) {
    int next = (kbd_tail + 1) % KBD_BUF_SIZE;
    if (next != kbd_head) {
        kbd_buf[kbd_tail] = val;
        kbd_tail = next;
    }
}

// 从队列取一个按键事件
uint32_t kbd_pop() {
    if (kbd_head == kbd_tail) return KEY_NONE;
    uint32_t val = kbd_buf[kbd_head];
    kbd_head = (kbd_head + 1) % KBD_BUF_SIZE;
    return val;
}

// 处理 SDL 事件（由主循环调用）
void process_sdl_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                // 窗口关闭按钮 → 触发 ebreak
                extern bool g_quit_requested;
                g_quit_requested = true;
                break;
            case SDL_KEYDOWN: {
                uint32_t am_key = sdl_to_am(e.key.keysym.scancode);
                if (am_key) kbd_push(KEY_DOWN | am_key);
                break;
            }
            case SDL_KEYUP: {
                uint32_t am_key = sdl_to_am(e.key.keysym.scancode);
                if (am_key) kbd_push(am_key);   // 松开：高位为 0
                break;
            }
        }
    }
}

uint32_t kbd_mmio_read() {
    process_sdl_events();   // 顺带处理所有 SDL 事件
    return kbd_pop();
}
```

---

## 四、集成到内存访问路径

修改 `score/src/memory/memory.cpp`，将所有外设接入 MMIO 分发：

```cpp
#include "mmio.h"
#include <ctime>
#include <cstdio>

// 前向声明（来自各设备文件）
void     display_init();
void     display_mmio_write(uint32_t addr, int len, uint32_t data);
uint32_t display_mmio_read(uint32_t addr, int len);
uint32_t kbd_mmio_read();

static uint32_t mmio_read(uint32_t addr, int len) {
    // 定时器
    if (addr == TIMER_LO_ADDR) {
        struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t us = (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
        return (uint32_t)(us & 0xffffffff);
    }
    if (addr == TIMER_HI_ADDR) {
        struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t us = (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
        return (uint32_t)(us >> 32);
    }
    // 键盘
    if (addr == KBD_ADDR) return kbd_mmio_read();
    // VGA 控制 / 帧缓冲
    if (addr == VGACTL_ADDR || (addr >= FB_ADDR))
        return display_mmio_read(addr, len);
    return 0;
}

static void mmio_write(uint32_t addr, int len, uint32_t data) {
    // UART
    if (addr == UART_TX_ADDR) {
        putchar((char)(data & 0xff));
        fflush(stdout);
        return;
    }
    // 帧缓冲
    if (addr >= FB_ADDR) {
        display_mmio_write(addr, len, data);
        return;
    }
}
```

---

## 五、SCore 主函数初始化 SDL

修改 `score/src/main.cpp`，加入 SDL 初始化：

```cpp
// 在 sdb_mainloop() 之前调用
display_init();
```

在 `execute.cpp` 的主循环中加入事件处理（确保窗口能响应关闭等事件）：

```cpp
// 在 cpu_exec 每执行 1000 条指令时轮询一次 SDL 事件
extern void process_sdl_events();
if (i % 1000 == 0) process_sdl_events();
```

`Makefile` 中添加 SDL2 链接：

```makefile
LDFLAGS := -lreadline $(shell sdl2-config --libs)
CXXFLAGS += $(shell sdl2-config --cflags)
```

---

## 六、独立验证（不依赖 B 的 SHAL 代码）

A 可以自己写一个简单的汇编程序，直接向帧缓冲写颜色值，验证显示是否正常：

```asm
# test_vga.S：将屏幕全部填充为蓝色（0xFF0000FF）
.section .text
.globl _start
_start:
    li   t0, 0xa1000000     # 帧缓冲起始
    li   t1, 0xff0000ff     # 蓝色（ARGB）
    li   t2, 120000         # 400*300 = 120000 个像素
1:
    sw   t1, 0(t0)
    addi t0, t0, 4
    addi t2, t2, -1
    bnez t2, 1b
    ebreak
```

```bash
riscv32-unknown-elf-gcc -march=rv32im -mabi=ilp32 -nostdlib \
    -T stratum-apps/cpu-tests/link.ld test_vga.S -o test_vga.elf
./score/score -e test_vga.elf
# 期望：SDL2 窗口变为纯蓝色
```

---

## 七、本阶段完成检查清单

- [ ] SDL2 窗口能正常弹出（`display_init()` 无报错）
- [ ] VGA 填色测试：窗口变为指定颜色
- [ ] 键盘测试：按 A 键后 `kbd_mmio_read()` 返回非零值
- [ ] 定时器读取：两次读取之间差值合理
- [ ] 关闭 SDL 窗口后 SCore 能正常退出
- [ ] `mmio.h` 中所有地址已提交，B 可以直接引用

---

## 八、提交本阶段成果

```cpp
git add score/ stratum-apps/cpu-tests/
git commit -m "pa3-a: add VGA/keyboard/timer device simulation via SDL2"
git push origin feat/pa3-devices
```

> 不要向 `dev` 合入，等成员 B 完成 PA3-B 后，进入 [联调3] 再合并。

---

*本阶段完成后，等待成员 B 完成 [PA3-B-SHAL接口]，再进入 [联调3-IO设备打通]。*
