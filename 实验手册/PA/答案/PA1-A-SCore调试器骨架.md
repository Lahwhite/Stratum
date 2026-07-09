# PA1-A — SCore 调试器骨架
> 成员 A 独立完成 · 前置：PA0 完成 · 预计工时：2～3 天

---

## 本手册范围说明

本手册仅包含**成员 A 的工作**：

- SCore 主循环框架
- SDB 命令解析与分发框架
- `si`（单步执行）命令
- `info r`（打印寄存器）命令
- `x`（内存查看）命令
- `q`（退出）命令

**不包含**：表达式求值（`p` 命令）、watchpoint（`w`/`d` 命令）——这两部分由成员 B 在 [PA1-B] 中完成，联调时再合并。

---

## 目标

在 SCore 中建立一个可交互的调试命令循环，能够控制 CPU 模拟器的运行，查看寄存器和内存状态。

**本阶段完成标志：**
- `./score` 启动后进入 `(sdb)` 命令提示符
- `si [N]` 能单步执行（当前 CPU 尚未实现，打印提示即可）
- `info r` 打印所有寄存器（当前为初始值 0）
- `x N ADDR` 打印内存内容（当前为初始值 0）
- `q` 正常退出
- `help` 打印所有命令说明

---

## 一、CPU 状态结构体定义

这是两人之间最重要的共享接口，**由成员 A 定义，提交到 dev 后成员 B 即可使用**。

创建 `score/include/cpu.h`：

```cpp
#pragma once
#include <cstdint>

// RISC-V 32 位 CPU 状态
struct CPU_state {
    uint32_t gpr[32];   // x0 ~ x31 通用寄存器
    uint32_t pc;        // 程序计数器

    // 寄存器别名访问（ABI 名称）
    uint32_t& zero() { return gpr[0]; }
    uint32_t& ra()   { return gpr[1]; }
    uint32_t& sp()   { return gpr[2]; }
    uint32_t& gp()   { return gpr[3]; }
    uint32_t& tp()   { return gpr[4]; }
    // t0~t2: gpr[5~7], s0/fp: gpr[8], s1: gpr[9]
    // a0~a7: gpr[10~17], s2~s11: gpr[18~27]
    // t3~t6: gpr[28~31]
};

// RISC-V ABI 寄存器名称表
static const char* GPR_NAMES[32] = {
    "zero","ra","sp","gp","tp",
    "t0","t1","t2","s0","s1",
    "a0","a1","a2","a3","a4","a5","a6","a7",
    "s2","s3","s4","s5","s6","s7","s8","s9","s10","s11",
    "t3","t4","t5","t6"
};

// 全局 CPU 状态（在 cpu.cpp 中定义）
extern CPU_state cpu;
```

创建 `score/src/cpu/cpu.cpp`：

```cpp
#include "cpu.h"

CPU_state cpu = {};   // 初始化为全 0
```

---

## 二、内存模块桩（Stub）

PA2 阶段 A 才会完整实现内存。此处先建一个空壳，让 SDB 的 `x` 命令能编译通过。

创建 `score/include/memory.h`：

```cpp
#pragma once
#include <cstdint>

// 读取物理地址处的数据（1/2/4 字节）
uint32_t paddr_read(uint32_t addr, int len);

// 写入物理地址处的数据
void paddr_write(uint32_t addr, int len, uint32_t data);
```

创建 `score/src/memory/memory.cpp`：

```cpp
#include "memory.h"
#include <cstdio>

// 暂时返回 0，PA2 阶段替换为真实内存
uint32_t paddr_read(uint32_t addr, int len) {
    return 0;
}

void paddr_write(uint32_t addr, int len, uint32_t data) {
    // stub
}
```

---

## 三、SDB 命令循环

### 3.1 SDB 头文件

创建 `score/include/sdb.h`：

```cpp
#pragma once

// 启动调试器主循环
void sdb_mainloop();
```

### 3.2 SDB 实现

创建 `score/src/monitor/sdb.cpp`：

```cpp
#include "sdb.h"
#include "cpu.h"
#include "memory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <readline/readline.h>
#include <readline/history.h>

// ─── 前向声明（表达式求值由成员 B 实现，PA1联调后接入）─────────────────
// uint32_t expr_eval(const char *e, bool *success);
// ─────────────────────────────────────────────────────────────────────────

// 执行单步
static int cmd_si(char *args) {
    int n = 1;
    if (args != nullptr) n = atoi(args);
    if (n <= 0) n = 1;
    printf("[SCore] si: step %d instruction(s) (CPU not yet implemented)\n", n);
    // PA2 完成后替换为: cpu_exec(n);
    return 0;
}

// 打印寄存器
static int cmd_info(char *args) {
    if (args == nullptr || strncmp(args, "r", 1) != 0) {
        printf("Usage: info r\n");
        return 0;
    }
    printf("%-8s 0x%08x\n", "pc", cpu.pc);
    for (int i = 0; i < 32; i++) {
        printf("%-8s 0x%08x", GPR_NAMES[i], cpu.gpr[i]);
        if (i % 4 == 3) printf("\n");
        else            printf("  ");
    }
    return 0;
}

// 查看内存
static int cmd_x(char *args) {
    if (args == nullptr) {
        printf("Usage: x N ADDR\n");
        return 0;
    }
    int n;
    uint32_t addr;
    if (sscanf(args, "%d %x", &n, &addr) != 2) {
        printf("Usage: x N ADDR  (ADDR in hex, e.g. x 4 0x80000000)\n");
        return 0;
    }
    for (int i = 0; i < n; i++) {
        if (i % 4 == 0) printf("0x%08x: ", addr + i * 4);
        printf("0x%08x  ", paddr_read(addr + i * 4, 4));
        if (i % 4 == 3) printf("\n");
    }
    if (n % 4 != 0) printf("\n");
    return 0;
}

// 打印帮助
static int cmd_help(char *args);

// ─── 命令表 ──────────────────────────────────────────────────────────────
struct Command {
    const char *name;
    const char *desc;
    int (*handler)(char *args);
};

static Command cmd_table[] = {
    {"help", "打印本帮助信息",                           cmd_help},
    {"q",    "退出 SDB",                                 nullptr },
    {"si",   "si [N]  单步执行 N 条指令（默认 1）",      cmd_si  },
    {"info", "info r  打印寄存器状态",                    cmd_info},
    {"x",    "x N ADDR  查看从 ADDR 起 N 个字的内存",    cmd_x   },
    // p / w / d 由成员 B 实现，联调后在此追加
};
static const int NR_CMD = sizeof(cmd_table) / sizeof(cmd_table[0]);

static int cmd_help(char *args) {
    printf("可用命令：\n");
    for (int i = 0; i < NR_CMD; i++) {
        printf("  %-8s  %s\n", cmd_table[i].name, cmd_table[i].desc);
    }
    return 0;
}

// ─── 主循环 ──────────────────────────────────────────────────────────────
void sdb_mainloop() {
    const char *PROMPT = "(sdb) ";
    char *line;

    while ((line = readline(PROMPT)) != nullptr) {
        if (*line) add_history(line);

        // 分割命令和参数
        char *cmd  = strtok(line, " \t");
        char *args = strtok(nullptr, "");   // 剩余部分作为 args

        if (cmd == nullptr) { free(line); continue; }

        // 退出
        if (strcmp(cmd, "q") == 0) { free(line); break; }

        // 查找并执行命令
        bool found = false;
        for (int i = 0; i < NR_CMD; i++) {
            if (strcmp(cmd, cmd_table[i].name) == 0) {
                cmd_table[i].handler(args);
                found = true;
                break;
            }
        }
        if (!found) {
            printf("未知命令: %s，输入 help 查看所有命令\n", cmd);
        }

        free(line);
    }

    printf("Bye.\n");
}
```

> **注意**：需要链接 `readline` 库。在 `score/Makefile` 中添加：
> ```makefile
> LDFLAGS := -lreadline
> ```

### 3.3 更新 main.cpp

修改 `score/src/main.cpp`：

```cpp
#include <cstdio>
#include "sdb.h"

int main(int argc, char *argv[]) {
    printf("Stratum-Core (SCore) - RISC-V 32-bit Simulator\n");
    printf("Build: " __DATE__ " " __TIME__ "\n\n");
    sdb_mainloop();
    return 0;
}
```

---

## 四、更新 Makefile

安装 readline：

```bash
sudo apt install -y libreadline-dev
```

更新 `score/Makefile`，确保链接 readline：

```makefile
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g
LDFLAGS  := -lreadline
TARGET   := score
SRCS     := $(shell find src -name '*.cpp')
OBJS     := $(SRCS:.cpp=.o)
INC      := -Iinclude

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INC) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
```

---

## 五、编译与验证

```bash
cd score
make
./score
```

期望交互：

```
Stratum-Core (SCore) - RISC-V 32-bit Simulator
Build: Jul 02 2026 ...

(sdb) help
可用命令：
  help      打印本帮助信息
  q         退出 SDB
  si        si [N]  单步执行 N 条指令（默认 1）
  info      info r  打印寄存器状态
  x         x N ADDR  查看从 ADDR 起 N 个字的内存

(sdb) info r
pc       0x00000000
zero     0x00000000  ra       0x00000000  sp       0x00000000  gp       0x00000000
tp       0x00000000  t0       0x00000000  t1       0x00000000  t2       0x00000000
...

(sdb) x 4 0x0
0x00000000: 0x00000000  0x00000000  0x00000000  0x00000000

(sdb) si 3
[SCore] si: step 3 instruction(s) (CPU not yet implemented)

(sdb) q
Bye.
```

---

## 六、提交本阶段成果

```bash
git add score/
git commit -m "pa1-a: add SDB skeleton with si/info/x commands"
git push origin feat/pa1-score-skeleton
```

> 不要向 `dev` 合入，等成员 B 完成 PA1-B 后，两人一起进行 [联调1] 再合并。

---

## 七、本阶段完成检查清单

- [ ] `make` 编译通过，无 error
- [ ] `./score` 进入 `(sdb)` 提示符
- [ ] `help` 列出所有命令
- [ ] `info r` 打印 33 行（pc + 32 个寄存器），格式整齐
- [ ] `x 4 0x0` 打印 4 个字的内存内容
- [ ] `si 5` 打印提示信息
- [ ] `q` 正常退出，打印 Bye.
- [ ] 未知命令有友好提示

---

*本阶段完成后，等待成员 B 完成 [PA1-B-表达式解析]，再进入 [联调1-PA1合并集成]。*
