# 联调3 — IO 设备打通
> 双人共同完成 · 前置：PA3-A 和 PA3-B 均已完成 · 预计工时：1～1.5 天

---

## 目标

将 A 的 SCore 外设 MMIO 与 B 的 SHAL IOE 接口对接，逐设备打通串口→定时器→键盘→VGA，最终在 SCore 上运行打字游戏 Demo。通过后打 tag `v3.0-pa3`。

---

## 一、合并到 dev

```bash
git checkout dev && git pull origin dev
git merge --no-ff feat/pa3-devices -m "merge: pa3-a SDL2 device MMIO"
git merge --no-ff feat/pa3-shal    -m "merge: pa3-b SHAL IOE interface"
```

冲突处理原则：
- `mmio.h`：以 A 的版本为准（地址定义权在 A）
- `amdev.h`：以 B 的版本为准（接口定义权在 B）
- `ioe.cpp` riscv 版：以 B 的为准，但地址常量需要与 A 的 `mmio.h` 核对

---

## 二、逐步对接（按设备顺序）

### Step 1：串口（UART）

串口已在联调2中验证过，此处仅确认扩充后的接口编号没有冲突。

```bash
# 编译一个只用 printf 的 hello 程序
cd stratum-apps/apps/hello
make ARCH=riscv32-score
./score/score -e hello_c.elf
```

期望：终端正常打印 `Hello from Stratum!`

若不正常：检查 `AM_UART_TX` 编号（B 改为 11，A 侧 MMIO 拦截的地址逻辑与编号无关，但确认 `ioe_write(AM_UART_TX)` → `MMIO_WR(0xa0000000, data)` 路径无断层）

---

### Step 2：定时器

编写定时器测试程序 `stratum-apps/hal-tests/test_timer.c`：

```c
#include "am.h"
#include "amdev.h"
#include <stdio.h>

int main() {
    ioe_init();
    uint64_t t0 = io_read(AM_TIMER_UPTIME).us;
    // 空循环约 0.1 秒
    for (volatile int i = 0; i < 5000000; i++);
    uint64_t t1 = io_read(AM_TIMER_UPTIME).us;
    printf("经过时间: %llu us\n", (unsigned long long)(t1 - t0));
    // 期望：50000 ~ 500000 us（取决于 SCore 指令执行速度）
    return 0;
}
```

在 SCore 上运行：

```bash
./score/score -e stratum-apps/hal-tests/test_timer.elf
```

验证标准：两次读取差值 > 0，且数量级合理（不为零，不溢出）。

若定时器返回 0：
- A：检查 `TIMER_LO_ADDR` 的 MMIO 读逻辑，确认 `clock_gettime` 被调用
- B：检查 riscv `ioe.cpp` 中 `MMIO_RD(0xa0000048u)` 的地址与 A 的 `mmio.h` 一致

---

### Step 3：键盘

编写键盘测试程序 `stratum-apps/hal-tests/test_kbd.c`：

```c
#include "am.h"
#include "amdev.h"
#include <stdio.h>

int main() {
    ioe_init();
    printf("请按任意字母键（ESC 退出）...\n");
    while (1) {
        AM_INPUT_KEYBRD_T key = io_read(AM_INPUT_KEYBRD);
        if (key.keycode == AM_KEY_NONE) continue;
        printf("%s 键码 %d\n", key.keydown ? "按下" : "松开", key.keycode);
        if (key.keydown && key.keycode == AM_KEY_ESCAPE) break;
    }
    printf("键盘测试完成\n");
    return 0;
}
```

在 SCore 上运行：

```bash
./score/score -e stratum-apps/hal-tests/test_kbd.elf
```

验证：按下 A 键 → 打印"按下 键码 1"；松开 → 打印"松开 键码 1"；ESC 退出。

常见问题排查：

| 现象 | 先查谁 | 具体排查 |
|------|--------|----------|
| 完全没有按键输出 | A | `kbd_mmio_read()` 是否在 MMIO 路径中被调用；SDL 事件是否在 `process_sdl_events()` 中被处理 |
| 有输出但键码全错 | A | `sdl_to_am()` 映射表是否与 B 的 `AMKey` 枚举一一对应 |
| keydown/keyup 混淆 | A/B | A 的 `KEY_DOWN` bit 位置（bit31）与 B 解析 `raw >> 31` 是否一致 |

---

### Step 4：VGA 显示

编写 VGA 测试程序 `stratum-apps/hal-tests/test_vga.c`：

```c
#include "am.h"
#include "amdev.h"
#include <stdio.h>

int main() {
    ioe_init();
    AM_GPU_CONFIG_T cfg = io_read(AM_GPU_CONFIG);
    printf("屏幕: %d x %d\n", cfg.width, cfg.height);

    // 画彩虹色竖条
    static uint32_t row[400];
    uint32_t colors[] = {
        0xffff0000u, 0xffff7f00u, 0xffffff00u,
        0xff00ff00u, 0xff0000ffu, 0xff8b00ffu
    };
    for (int y = 0; y < cfg.height; y++) {
        for (int x = 0; x < cfg.width; x++) {
            row[x] = colors[(x * 6) / cfg.width];
        }
        io_write(AM_GPU_FBDRAW, 0, y, cfg.width, 1, row);
    }
    printf("VGA 测试完成，窗口应显示彩虹竖条\n");

    // 等待 ESC 退出
    while (1) {
        AM_INPUT_KEYBRD_T key = io_read(AM_INPUT_KEYBRD);
        if (key.keydown && key.keycode == AM_KEY_ESCAPE) break;
    }
    return 0;
}
```

验证：SDL2 窗口显示 6 色彩虹竖条（红→橙→黄→绿→蓝→紫）。

常见问题：

| 现象 | 先查谁 | 具体排查 |
|------|--------|----------|
| 窗口不弹出 | A | `display_init()` 是否在 `main()` 中被调用 |
| 全黑无颜色 | A | 帧缓冲地址 `FB_ADDR` MMIO 写入是否触发 `display_mmio_write` |
| 颜色显示错误 | B | riscv `ioe.cpp` 中写帧缓冲时像素格式是否为 ARGB8888 |
| 画面只有一部分 | B | `AM_GPU_FBDRAW` 的 x/y/w/h 坐标计算是否正确 |

---

### Step 5：打字游戏完整运行

```bash
# 编译打字游戏（ARCH=riscv32-score）
cd stratum-apps/apps/typing
make ARCH=riscv32-score
./score/score -e typing.elf
```

验证标准：
- SDL2 窗口中心显示一个绿色色块代表目标字母
- 按下对应字母键后终端打印"正确！"并切换下一个字母
- 按 ESC 退出并打印最终得分

---

## 三、联调完成检查清单

- [ ] 串口：`printf` 在 SCore 上输出正确
- [ ] 定时器：两次读取差值 > 0
- [ ] 键盘：按键事件正确（keydown/keyup 均识别，键码正确）
- [ ] VGA：彩虹竖条正常显示
- [ ] 打字游戏：能完整运行，得分和按键响应正常
- [ ] 关闭 SDL 窗口不崩溃

---

## 四、合入 main 并打 tag

```bash
git checkout dev
make clean && make
# 发 PR dev → main，对方 Review
git checkout main && git pull origin main
git tag v3.0-pa3
git push origin v3.0-pa3
```

---

*联调3 完成后，成员 A 进入 [PA4-A-中断与陷入]，成员 B 进入 [PA4-B-SOS操作系统]，两人再次独立并行。*
*此时 B 的 PA4 手册中包含联调3 已验证的成果（完整的 SHAL IOE 接口），可以直接作为 SOS 的基础使用。*
