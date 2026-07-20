# PA3-B — SHAL 完整 IOE 接口
> 成员 B 独立完成 · 前置：联调2 完成（v2.0-pa2 已打 tag）· 预计工时：3～4 天

---

## 本手册范围说明

本手册仅包含**成员 B 的工作**：

- 补全 `amdev.h` 中的全部设备接口定义（键盘、定时器、VGA）
- native 平台的完整 IOE 实现（用 SDL2 直接运行，不经过 SCore）
- riscv 平台的 IOE 实现（通过 MMIO 访问 SCore 模拟的外设）
- hal-tests 测试套件（native 平台验证）
- 演示程序框架（打字游戏骨架）

**不包含**：SCore 侧的外设 MMIO 寄存器实现——那由成员 A 在 PA3-A 中完成。

**前置可用成果（来自联调2）：**
- 联调2 已确认的 MMIO 地址表（`mmio.h`，由 A 定义）
- klib 的 `printf`、`string` 函数可用
- native 平台已能直接运行

---

## 目标

**本阶段完成标志：**
- `ARCH=native make` 运行 hal-tests 全部通过
- native 平台能弹出 SDL2 窗口，能读取按键事件
- 演示打字游戏能在 native 平台运行（不依赖 SCore）

---

## 一、补全 amdev.h

扩充 `shal/include/amdev.h`，加入所有 PA3 设备：

```cpp
#pragma once
#include <cstdint>

// ─── 定时器 ──────────────────────────────────────────────────────────────
#define AM_TIMER_CONFIG   0   // 读：是否支持定时器
#define AM_TIMER_UPTIME   1   // 读：系统运行时间（微秒）
typedef struct { bool present; }   AM_TIMER_CONFIG_T;
typedef struct { uint64_t us; }    AM_TIMER_UPTIME_T;

// ─── 串口 ────────────────────────────────────────────────────────────────
#define AM_UART_CONFIG    10  // 读：是否支持串口
#define AM_UART_TX        11  // 写：发送一个字符
#define AM_UART_RX        12  // 读：接收一个字符（未就绪时返回 '\xff'）
typedef struct { bool present; }   AM_UART_CONFIG_T;
typedef struct { char data; }      AM_UART_TX_T;
typedef struct { char data; }      AM_UART_RX_T;

// ─── 键盘 ────────────────────────────────────────────────────────────────
#define AM_INPUT_CONFIG   20  // 读：是否支持键盘
#define AM_INPUT_KEYBRD   21  // 读：当前按键事件
typedef struct { bool present; }            AM_INPUT_CONFIG_T;
typedef struct { bool keydown; uint8_t keycode; } AM_INPUT_KEYBRD_T;

// AM 标准按键码
enum AMKey {
    AM_KEY_NONE = 0,
    AM_KEY_A = 1, AM_KEY_B, AM_KEY_C, AM_KEY_D, AM_KEY_E,
    AM_KEY_F, AM_KEY_G, AM_KEY_H, AM_KEY_I, AM_KEY_J,
    AM_KEY_K, AM_KEY_L, AM_KEY_M, AM_KEY_N, AM_KEY_O,
    AM_KEY_P, AM_KEY_Q, AM_KEY_R, AM_KEY_S, AM_KEY_T,
    AM_KEY_U, AM_KEY_V, AM_KEY_W, AM_KEY_X, AM_KEY_Y, AM_KEY_Z,
    AM_KEY_1, AM_KEY_2, AM_KEY_3, AM_KEY_4, AM_KEY_5,
    AM_KEY_6, AM_KEY_7, AM_KEY_8, AM_KEY_9, AM_KEY_0,
    AM_KEY_RETURN = 41, AM_KEY_ESCAPE = 40, AM_KEY_SPACE = 42,
    AM_KEY_LEFT = 80, AM_KEY_RIGHT, AM_KEY_UP, AM_KEY_DOWN,
};

// ─── GPU / 帧缓冲 ─────────────────────────────────────────────────────────
#define AM_GPU_CONFIG     30  // 读：屏幕宽高
#define AM_GPU_STATUS     31  // 读：GPU 忙碌状态
#define AM_GPU_FBDRAW     32  // 写：绘制像素矩形
#define AM_GPU_MEMCPY     33  // 写：帧缓冲区内复制
#define AM_GPU_RENDER     34  // 写：提交帧
typedef struct { bool present; int width, height; }  AM_GPU_CONFIG_T;
typedef struct { bool ready; }                        AM_GPU_STATUS_T;
typedef struct { int x, y, w, h; uint32_t *pixels; } AM_GPU_FBDRAW_T;
typedef struct { } AM_GPU_RENDER_T;
```

---

## 二、native 平台 IOE 实现

native 平台使用 SDL2 直接显示，不经过 SCore 的 MMIO。

修改/扩充 `shal/src/native/ioe.cpp`：

```cpp
#include "am.h"
#include "amdev.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <SDL2/SDL.h>

static SDL_Window   *win = nullptr;
static SDL_Renderer *ren = nullptr;
static SDL_Texture  *tex = nullptr;
static int screen_w = 400, screen_h = 300;
static uint32_t *framebuf = nullptr;

// 按键事件队列（环形缓冲）
static const int KBD_BUF = 64;
struct KeyEvt { bool down; uint8_t code; };
static KeyEvt  kbd_buf[KBD_BUF];
static int     kbd_head = 0, kbd_tail = 0;

static uint8_t sdl_sc_to_am(SDL_Scancode sc) {
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
        return (uint8_t)(sc - SDL_SCANCODE_A + AM_KEY_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
        return (uint8_t)(sc - SDL_SCANCODE_1 + AM_KEY_1);
    if (sc == SDL_SCANCODE_0)      return AM_KEY_0;
    if (sc == SDL_SCANCODE_RETURN) return AM_KEY_RETURN;
    if (sc == SDL_SCANCODE_ESCAPE) return AM_KEY_ESCAPE;
    if (sc == SDL_SCANCODE_SPACE)  return AM_KEY_SPACE;
    if (sc == SDL_SCANCODE_LEFT)   return AM_KEY_LEFT;
    if (sc == SDL_SCANCODE_RIGHT)  return AM_KEY_RIGHT;
    if (sc == SDL_SCANCODE_UP)     return AM_KEY_UP;
    if (sc == SDL_SCANCODE_DOWN)   return AM_KEY_DOWN;
    return AM_KEY_NONE;
}

static void poll_sdl() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) exit(0);
        if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
            uint8_t code = sdl_sc_to_am(e.key.keysym.scancode);
            if (code) {
                int next = (kbd_tail + 1) % KBD_BUF;
                if (next != kbd_head) {
                    kbd_buf[kbd_tail] = {e.type == SDL_KEYDOWN, code};
                    kbd_tail = next;
                }
            }
        }
    }
}

bool ioe_init() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return false;
    win = SDL_CreateWindow("Stratum (native)", SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED, screen_w, screen_h,
                           SDL_WINDOW_SHOWN);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                            SDL_TEXTUREACCESS_STREAMING, screen_w, screen_h);
    framebuf = (uint32_t*)calloc(screen_w * screen_h, 4);
    return true;
}

void ioe_read(uintptr_t reg, void *buf) {
    switch (reg) {
        case AM_TIMER_CONFIG:
            ((AM_TIMER_CONFIG_T*)buf)->present = true; break;
        case AM_TIMER_UPTIME: {
            struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
            ((AM_TIMER_UPTIME_T*)buf)->us =
                (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
            break;
        }
        case AM_UART_CONFIG:
            ((AM_UART_CONFIG_T*)buf)->present = true; break;
        case AM_INPUT_CONFIG:
            ((AM_INPUT_CONFIG_T*)buf)->present = true; break;
        case AM_INPUT_KEYBRD: {
            poll_sdl();
            auto *k = (AM_INPUT_KEYBRD_T*)buf;
            if (kbd_head == kbd_tail) {
                k->keydown = false; k->keycode = AM_KEY_NONE;
            } else {
                KeyEvt evt = kbd_buf[kbd_head];
                kbd_head = (kbd_head + 1) % KBD_BUF;
                k->keydown = evt.down; k->keycode = evt.code;
            }
            break;
        }
        case AM_GPU_CONFIG: {
            auto *g = (AM_GPU_CONFIG_T*)buf;
            g->present = true; g->width = screen_w; g->height = screen_h;
            break;
        }
        case AM_GPU_STATUS:
            ((AM_GPU_STATUS_T*)buf)->ready = true; break;
        default: break;
    }
}

void ioe_write(uintptr_t reg, const void *buf) {
    switch (reg) {
        case AM_UART_TX:
            putchar(((AM_UART_TX_T*)buf)->data); fflush(stdout); break;
        case AM_GPU_FBDRAW: {
            auto *d = (AM_GPU_FBDRAW_T*)buf;
            for (int row = 0; row < d->h; row++) {
                memcpy(framebuf + (d->y + row) * screen_w + d->x,
                       d->pixels + row * d->w,
                       d->w * 4);
            }
            SDL_UpdateTexture(tex, nullptr, framebuf, screen_w * 4);
            SDL_RenderCopy(ren, tex, nullptr, nullptr);
            SDL_RenderPresent(ren);
            poll_sdl();
            break;
        }
        case AM_GPU_RENDER:
            SDL_RenderPresent(ren); poll_sdl(); break;
        default: break;
    }
}
```

---

## 三、riscv 平台 IOE 实现

更新 `shal/src/riscv/ioe.cpp`，补全键盘和 GPU 部分：

```cpp
#include "am.h"
#include "amdev.h"
#include <cstring>

// 从 A 提交的 mmio.h 中获取地址
// 直接用 volatile 指针访问 MMIO
#define MMIO_RD(addr)       (*(volatile uint32_t*)(addr))
#define MMIO_WR(addr, val)  (*(volatile uint32_t*)(addr) = (val))

bool ioe_init() { return true; }

void ioe_read(uintptr_t reg, void *buf) {
    switch (reg) {
        case AM_TIMER_UPTIME: {
            uint32_t lo = MMIO_RD(0xa0000048u);
            uint32_t hi = MMIO_RD(0xa000004cu);
            ((AM_TIMER_UPTIME_T*)buf)->us = ((uint64_t)hi << 32) | lo;
            break;
        }
        case AM_INPUT_KEYBRD: {
            uint32_t raw = MMIO_RD(0xa0000060u);
            auto *k = (AM_INPUT_KEYBRD_T*)buf;
            k->keydown = (raw >> 31) & 1;
            k->keycode = raw & 0x7fu;
            break;
        }
        case AM_GPU_CONFIG: {
            uint32_t raw = MMIO_RD(0xa0000100u);
            auto *g = (AM_GPU_CONFIG_T*)buf;
            g->present = true;
            g->width   = raw >> 16;
            g->height  = raw & 0xffffu;
            break;
        }
        case AM_UART_CONFIG:
            ((AM_UART_CONFIG_T*)buf)->present = true; break;
        case AM_INPUT_CONFIG:
            ((AM_INPUT_CONFIG_T*)buf)->present = true; break;
        default: break;
    }
}

void ioe_write(uintptr_t reg, const void *buf) {
    switch (reg) {
        case AM_UART_TX:
            *(volatile uint8_t*)0xa0000000u = ((AM_UART_TX_T*)buf)->data;
            break;
        case AM_GPU_FBDRAW: {
            // 将像素矩形写入帧缓冲（位于 0xa1000000）
            auto *d = (AM_GPU_FBDRAW_T*)buf;
            // 需要从 AM_GPU_CONFIG 取屏幕宽度
            AM_GPU_CONFIG_T cfg;
            ioe_read(AM_GPU_CONFIG, &cfg);
            volatile uint32_t *fb = (volatile uint32_t*)0xa1000000u;
            for (int row = 0; row < d->h; row++) {
                for (int col = 0; col < d->w; col++) {
                    fb[(d->y + row) * cfg.width + (d->x + col)]
                        = d->pixels[row * d->w + col];
                }
            }
            break;
        }
        default: break;
    }
}
```

---

## 四、hal-tests 测试套件（native 平台）

创建 `stratum-apps/hal-tests/test_ioe.cpp`：

```cpp
// 编译：g++ -std=c++17 -I shal/include shal/src/native/ioe.cpp
//           stratum-apps/hal-tests/test_ioe.cpp $(sdl2-config --libs --cflags) -o test_ioe
#include "am.h"
#include "amdev.h"
#include <cstdio>

int main() {
    if (!ioe_init()) {
        printf("IOE 初始化失败\n");
        return 1;
    }

    // 测试定时器
    AM_TIMER_UPTIME_T t1 = io_read(AM_TIMER_UPTIME);
    for (volatile int i = 0; i < 1000000; i++);
    AM_TIMER_UPTIME_T t2 = io_read(AM_TIMER_UPTIME);
    printf("定时器差值: %llu us（应 > 0）\n", (unsigned long long)(t2.us - t1.us));

    // 测试 GPU 配置
    AM_GPU_CONFIG_T cfg = io_read(AM_GPU_CONFIG);
    printf("屏幕尺寸: %d x %d\n", cfg.width, cfg.height);

    // 测试 UART
    io_write(AM_UART_TX, 'O');
    io_write(AM_UART_TX, 'K');
    io_write(AM_UART_TX, '\n');

    // 测试 GPU 绘制：画一个红色矩形
    static uint32_t pixels[100 * 100];
    for (int i = 0; i < 100 * 100; i++) pixels[i] = 0xffff0000u;  // 红色
    io_write(AM_GPU_FBDRAW, 50, 50, 100, 100, pixels);

    printf("请观察 SDL2 窗口中心是否有红色矩形，按任意键继续...\n");

    // 等待一次按键事件
    while (true) {
        AM_INPUT_KEYBRD_T key = io_read(AM_INPUT_KEYBRD);
        if (key.keydown && key.keycode != AM_KEY_NONE) {
            printf("检测到按键: code=%d\n", key.keycode);
            break;
        }
    }

    printf("hal-tests PASS\n");
    return 0;
}
```

---

## 五、演示程序骨架（打字游戏）

创建 `stratum-apps/apps/typing/typing.cpp`，作为 PA3 的最终演示：

```cpp
#include "am.h"
#include "amdev.h"
#include <cstdio>
#include <cstring>

// 简单打字游戏：屏幕上显示一个字母，用户输入正确则得分
static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static void draw_char(int x, int y, char c, uint32_t color) {
    // 极简实现：画一个 8x8 的色块代表字符（联调后可替换为真正的字体渲染）
    static uint32_t block[8 * 8];
    for (int i = 0; i < 64; i++) block[i] = color;
    io_write(AM_GPU_FBDRAW, x, y, 8, 8, block);
}

static void clear_screen() {
    AM_GPU_CONFIG_T cfg = io_read(AM_GPU_CONFIG);
    static uint32_t row[800];
    memset(row, 0, sizeof(row));
    for (int y = 0; y < cfg.height; y++) {
        io_write(AM_GPU_FBDRAW, 0, y, cfg.width, 1, row);
    }
}

int main() {
    if (!ioe_init()) return 1;

    int score = 0;
    int idx   = 0;

    clear_screen();

    while (true) {
        char target = letters[idx % 26];

        // 显示目标字母（绿色）
        clear_screen();
        draw_char(196, 146, target, 0xff00ff00u);

        // 等待按键
        AM_INPUT_KEYBRD_T key;
        do { key = io_read(AM_INPUT_KEYBRD); } while (!key.keydown);

        // 判断是否正确
        uint8_t expected = AM_KEY_A + (target - 'A');
        if (key.keycode == expected) {
            score++;
            idx++;
            printf("正确！得分: %d\n", score);
        } else {
            printf("错误！目标: %c\n", target);
        }

        // 按 ESC 退出
        if (key.keycode == AM_KEY_ESCAPE) break;
    }

    printf("游戏结束，最终得分: %d\n", score);
    return 0;
}
```

---

## 六、本阶段完成检查清单

- [ ] `amdev.h` 包含所有设备类型和编号
- [ ] native `ioe_init()` 无报错，SDL2 窗口正常弹出
- [ ] `test_ioe` 通过：定时器差值 > 0，屏幕显示红色矩形，按键被识别
- [ ] riscv 平台的 `ioe.cpp` 编译通过（即使还不能运行）
- [ ] 打字游戏在 native 平台能运行

---

## 七、提交本阶段成果

```bash
git add shal/ stratum-apps/
git commit -m "pa3-b: complete SHAL IOE interface for native and riscv platforms"
git push origin feat/pa3-shal
```

> 不要向 `dev` 合入，等成员 A 完成 PA3-A 后，进入 [联调3] 再合并。

---

*本阶段完成后，等待成员 A 完成 [PA3-A-外设寄存器]，再进入 [联调3-IO设备打通]。*
