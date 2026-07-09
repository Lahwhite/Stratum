# PA1-B — 表达式解析器
> 成员 B 独立完成 · 前置：PA0 完成 · 预计工时：2～3 天

---

## 本手册范围说明

本手册仅包含**成员 B 的工作**：

- 词法分析器（Lexer）：将表达式字符串切分为 Token
- 递归下降解析器（Parser）：对 Token 序列进行优先级计算，输出 uint32_t 结果
- `p EXPR` 命令实现
- `w EXPR` watchpoint 设置命令
- `d N` watchpoint 删除命令
- `info w` watchpoint 列表查看命令

**不包含**：SDB 主循环、`si`/`info r`/`x` 命令——这些由成员 A 在 [PA1-A] 中完成。

**前置依赖**：
- 需要使用成员 A 定义的 `score/include/cpu.h`（CPU_state 结构体）
- 编译时需要 A 提供的 `cpu.cpp`（全局 `cpu` 对象）

> **B 在开发阶段使用一个独立的 `test_main.cpp` 驱动自测，不依赖 A 的 `main.cpp`。联调时再将接口接入 A 的 SDB 主循环。**

---

## 目标

实现一个支持以下语法的表达式求值器，作为 `p`/`w` 命令的后端：

```
EXPR ::= NUMBER
       | HEXNUM
       | REG
       | '(' EXPR ')'
       | EXPR '+' EXPR
       | EXPR '-' EXPR
       | EXPR '*' EXPR
       | EXPR '/' EXPR
       | EXPR '==' EXPR
       | EXPR '!=' EXPR
       | EXPR '&&' EXPR
       | '*' EXPR          （解引用，读 4 字节内存）
```

**本阶段完成标志：**
- 表达式求值对 30 个测试用例全部正确
- `p 1+2*3` 输出 7
- `p $ra` 输出寄存器 ra 的值
- watchpoint 能正常添加、触发、删除

---

## 一、依赖说明

B 需要从 A 的分支 cherry-pick 或直接拿到以下两个文件：

```
score/include/cpu.h      ← A 已定义好 CPU_state 和 GPR_NAMES
score/src/cpu/cpu.cpp    ← 定义全局 cpu 对象
score/include/memory.h   ← paddr_read 接口（A 已提供桩）
score/src/memory/memory.cpp
```

如果 A 尚未 push，可以临时自己写一个最小的桩版本用于编译，联调时替换。

---

## 二、词法分析器

### 2.1 Token 类型定义

创建 `score/include/expr.h`：

```cpp
#pragma once
#include <cstdint>

// 对外接口：对字符串 e 求值，成功时 *success = true，失败时 = false
uint32_t expr_eval(const char *e, bool *success);
```

创建 `score/src/monitor/expr.cpp`，先写 Token 定义：

```cpp
#include "expr.h"
#include "cpu.h"
#include "memory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <regex.h>

// ─── Token 类型枚举 ───────────────────────────────────────────────────────
enum TokenType {
    TK_NOTYPE = 0,
    TK_NUM,        // 十进制整数
    TK_HEX,        // 0x 开头的十六进制数
    TK_REG,        // $寄存器名，如 $ra $a0
    TK_PLUS,       // +
    TK_MINUS,      // -
    TK_MUL,        // *
    TK_DIV,        // /
    TK_LPAREN,     // (
    TK_RPAREN,     // )
    TK_EQ,         // ==
    TK_NEQ,        // !=
    TK_AND,        // &&
    TK_DEREF,      // 一元 * （解引用，在 parse 阶段标记）
};

struct Token {
    TokenType type;
    char str[32];   // 原始字符串（寄存器名、数值）
};
```

### 2.2 正则规则定义

```cpp
// 词法规则：{ 正则表达式, Token 类型 }
struct Rule {
    const char *regex;
    TokenType   type;
};

static Rule rules[] = {
    {" +",          TK_NOTYPE},   // 空白，忽略
    {"0x[0-9a-fA-F]+", TK_HEX},  // 十六进制，必须在 TK_NUM 之前
    {"[0-9]+",      TK_NUM},
    {"\\$[a-z][a-z0-9]*", TK_REG},
    {"==",          TK_EQ},       // == 必须在 + 之前（防止误匹配）
    {"!=",          TK_NEQ},
    {"&&",          TK_AND},
    {"\\+",         TK_PLUS},
    {"-",           TK_MINUS},
    {"\\*",         TK_MUL},
    {"/",           TK_DIV},
    {"\\(",         TK_LPAREN},
    {"\\)",         TK_RPAREN},
};
static const int NR_RULES = sizeof(rules) / sizeof(rules[0]);

// 编译后的正则（只编译一次）
static regex_t re[NR_RULES];

// 模块初始化（在 expr_eval 首次调用时自动执行）
static void init_regex() {
    static bool inited = false;
    if (inited) return;
    inited = true;
    for (int i = 0; i < NR_RULES; i++) {
        int ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
        if (ret != 0) {
            char buf[128];
            regerror(ret, &re[i], buf, sizeof(buf));
            fprintf(stderr, "regex compile error: %s\n", buf);
            assert(0);
        }
    }
}
```

### 2.3 词法分析主函数

```cpp
static Token tokens[256];
static int   nr_token;

static bool tokenize(const char *e) {
    init_regex();
    nr_token = 0;
    int pos = 0;
    int len = strlen(e);

    while (pos < len) {
        bool matched = false;
        for (int i = 0; i < NR_RULES; i++) {
            regmatch_t pmatch;
            if (regexec(&re[i], e + pos, 1, &pmatch, 0) == 0
                && pmatch.rm_so == 0) {
                int mlen = pmatch.rm_eo;
                if (rules[i].type == TK_NOTYPE) {
                    pos += mlen;
                    matched = true;
                    break;
                }
                if (nr_token >= 256) {
                    fprintf(stderr, "表达式过长\n");
                    return false;
                }
                tokens[nr_token].type = rules[i].type;
                int copy = mlen < 31 ? mlen : 31;
                strncpy(tokens[nr_token].str, e + pos, copy);
                tokens[nr_token].str[copy] = '\0';
                nr_token++;
                pos += mlen;
                matched = true;
                break;
            }
        }
        if (!matched) {
            fprintf(stderr, "词法错误，无法识别字符 '%c' (位置 %d)\n", e[pos], pos);
            return false;
        }
    }
    return true;
}
```

---

## 三、递归下降解析器

### 3.1 一元 * 标记

在 parse 之前，扫描 token 序列，将作为解引用的 `*` 标记为 `TK_DEREF`：

```cpp
static void mark_deref() {
    for (int i = 0; i < nr_token; i++) {
        if (tokens[i].type == TK_MUL) {
            // 前一个 token 不是 ) / 数字 / 寄存器 → 是一元 *
            if (i == 0
                || (tokens[i-1].type != TK_RPAREN
                 && tokens[i-1].type != TK_NUM
                 && tokens[i-1].type != TK_HEX
                 && tokens[i-1].type != TK_REG)) {
                tokens[i].type = TK_DEREF;
            }
        }
    }
}
```

### 3.2 解析函数（优先级由低到高）

```cpp
// 前向声明
static uint32_t parse_expr(int *pos, bool *ok);
static uint32_t parse_and(int *pos, bool *ok);
static uint32_t parse_eq(int *pos, bool *ok);
static uint32_t parse_add(int *pos, bool *ok);
static uint32_t parse_mul(int *pos, bool *ok);
static uint32_t parse_unary(int *pos, bool *ok);
static uint32_t parse_primary(int *pos, bool *ok);

// EXPR → AND ( '&&' AND )*
static uint32_t parse_expr(int *pos, bool *ok) {
    uint32_t val = parse_and(pos, ok);
    while (*ok && *pos < nr_token && tokens[*pos].type == TK_AND) {
        (*pos)++;
        uint32_t rhs = parse_and(pos, ok);
        val = val && rhs ? 1 : 0;
    }
    return val;
}

// AND → EQ ( ('=='|'!=') EQ )*
static uint32_t parse_and(int *pos, bool *ok) {
    uint32_t val = parse_eq(pos, ok);
    while (*ok && *pos < nr_token
           && (tokens[*pos].type == TK_EQ || tokens[*pos].type == TK_NEQ)) {
        TokenType op = tokens[(*pos)++].type;
        uint32_t rhs = parse_eq(pos, ok);
        val = (op == TK_EQ) ? (val == rhs ? 1 : 0) : (val != rhs ? 1 : 0);
    }
    return val;
}

// EQ → ADD ( ('+'|'-') ADD )*
static uint32_t parse_eq(int *pos, bool *ok) {
    return parse_add(pos, ok);   // EQ 层直通，实际比较在 parse_and 处理
}

static uint32_t parse_add(int *pos, bool *ok) {
    uint32_t val = parse_mul(pos, ok);
    while (*ok && *pos < nr_token
           && (tokens[*pos].type == TK_PLUS || tokens[*pos].type == TK_MINUS)) {
        TokenType op = tokens[(*pos)++].type;
        uint32_t rhs = parse_mul(pos, ok);
        val = (op == TK_PLUS) ? val + rhs : val - rhs;
    }
    return val;
}

static uint32_t parse_mul(int *pos, bool *ok) {
    uint32_t val = parse_unary(pos, ok);
    while (*ok && *pos < nr_token
           && (tokens[*pos].type == TK_MUL || tokens[*pos].type == TK_DIV)) {
        TokenType op = tokens[(*pos)++].type;
        uint32_t rhs = parse_unary(pos, ok);
        if (op == TK_DIV && rhs == 0) {
            fprintf(stderr, "除零错误\n");
            *ok = false;
            return 0;
        }
        val = (op == TK_MUL) ? val * rhs : val / rhs;
    }
    return val;
}

static uint32_t parse_unary(int *pos, bool *ok) {
    if (*pos < nr_token && tokens[*pos].type == TK_DEREF) {
        (*pos)++;
        uint32_t addr = parse_unary(pos, ok);
        if (!*ok) return 0;
        return paddr_read(addr, 4);
    }
    if (*pos < nr_token && tokens[*pos].type == TK_MINUS) {
        (*pos)++;
        uint32_t val = parse_unary(pos, ok);
        return (uint32_t)(-(int32_t)val);
    }
    return parse_primary(pos, ok);
}

static uint32_t parse_primary(int *pos, bool *ok) {
    if (*pos >= nr_token) { *ok = false; return 0; }

    Token &t = tokens[*pos];

    if (t.type == TK_NUM) {
        (*pos)++;
        return (uint32_t)strtoul(t.str, nullptr, 10);
    }
    if (t.type == TK_HEX) {
        (*pos)++;
        return (uint32_t)strtoul(t.str + 2, nullptr, 16);   // 跳过 "0x"
    }
    if (t.type == TK_REG) {
        (*pos)++;
        // 匹配 pc
        if (strcmp(t.str + 1, "pc") == 0) return cpu.pc;
        // 匹配 ABI 名称
        for (int i = 0; i < 32; i++) {
            if (strcmp(t.str + 1, GPR_NAMES[i]) == 0) return cpu.gpr[i];
        }
        fprintf(stderr, "未知寄存器: %s\n", t.str);
        *ok = false;
        return 0;
    }
    if (t.type == TK_LPAREN) {
        (*pos)++;
        uint32_t val = parse_expr(pos, ok);
        if (!*ok) return 0;
        if (*pos >= nr_token || tokens[*pos].type != TK_RPAREN) {
            fprintf(stderr, "缺少右括号\n");
            *ok = false;
            return 0;
        }
        (*pos)++;
        return val;
    }

    fprintf(stderr, "意外的 token: %s\n", t.str);
    *ok = false;
    return 0;
}
```

### 3.3 对外接口实现

```cpp
uint32_t expr_eval(const char *e, bool *success) {
    *success = false;
    if (!tokenize(e)) return 0;
    mark_deref();

    int pos = 0;
    bool ok = true;
    uint32_t val = parse_expr(&pos, &ok);

    if (!ok || pos != nr_token) {
        if (ok) fprintf(stderr, "表达式未完全解析（剩余 token 从位置 %d 开始）\n", pos);
        return 0;
    }
    *success = true;
    return val;
}
```

---

## 四、Watchpoint 模块

### 4.1 数据结构

创建 `score/include/watchpoint.h`：

```cpp
#pragma once
#include <cstdint>

struct Watchpoint {
    int      id;
    char     expr[128];   // 监视的表达式字符串
    uint32_t last_val;    // 上次求值结果
    bool     in_use;
};

// 添加 watchpoint，返回编号；失败返回 -1
int  wp_add(const char *expr);

// 删除编号为 id 的 watchpoint
bool wp_delete(int id);

// 打印所有 watchpoint（info w 命令使用）
void wp_print_all();

// 在每条指令执行后检查所有 watchpoint（PA2 联调后由 cpu_exec 调用）
// 返回 true 表示有 watchpoint 触发，需要暂停
bool wp_check();
```

创建 `score/src/monitor/watchpoint.cpp`：

```cpp
#include "watchpoint.h"
#include "expr.h"
#include <cstdio>
#include <cstring>

static const int NR_WP = 32;
static Watchpoint wp_pool[NR_WP];
static int        next_id = 1;

static void init_pool() {
    static bool inited = false;
    if (inited) return;
    inited = true;
    memset(wp_pool, 0, sizeof(wp_pool));
}

int wp_add(const char *expr_str) {
    init_pool();
    for (int i = 0; i < NR_WP; i++) {
        if (!wp_pool[i].in_use) {
            wp_pool[i].in_use = true;
            wp_pool[i].id     = next_id++;
            strncpy(wp_pool[i].expr, expr_str, 127);
            wp_pool[i].expr[127] = '\0';

            bool ok;
            wp_pool[i].last_val = expr_eval(expr_str, &ok);
            if (!ok) {
                wp_pool[i].in_use = false;
                return -1;
            }
            printf("Watchpoint %d: %s\n", wp_pool[i].id, expr_str);
            return wp_pool[i].id;
        }
    }
    fprintf(stderr, "watchpoint 池已满（最多 %d 个）\n", NR_WP);
    return -1;
}

bool wp_delete(int id) {
    init_pool();
    for (int i = 0; i < NR_WP; i++) {
        if (wp_pool[i].in_use && wp_pool[i].id == id) {
            wp_pool[i].in_use = false;
            return true;
        }
    }
    return false;
}

void wp_print_all() {
    init_pool();
    bool any = false;
    for (int i = 0; i < NR_WP; i++) {
        if (wp_pool[i].in_use) {
            printf("Watchpoint %-3d  expr: %s  last_val: 0x%08x\n",
                   wp_pool[i].id, wp_pool[i].expr, wp_pool[i].last_val);
            any = true;
        }
    }
    if (!any) printf("（无 watchpoint）\n");
}

bool wp_check() {
    init_pool();
    bool triggered = false;
    for (int i = 0; i < NR_WP; i++) {
        if (!wp_pool[i].in_use) continue;
        bool ok;
        uint32_t cur = expr_eval(wp_pool[i].expr, &ok);
        if (!ok) continue;
        if (cur != wp_pool[i].last_val) {
            printf("Watchpoint %d: %s\n"
                   "  旧值: 0x%08x\n  新值: 0x%08x\n",
                   wp_pool[i].id, wp_pool[i].expr,
                   wp_pool[i].last_val, cur);
            wp_pool[i].last_val = cur;
            triggered = true;
        }
    }
    return triggered;
}
```

---

## 五、独立测试驱动

B 使用独立的 `test_main.cpp` 进行自测，**不依赖 A 的 main.cpp**。

创建 `score/src/monitor/test_expr.cpp`（仅测试用，联调后删除）：

```cpp
// 编译：g++ -std=c++17 -Iinclude expr.cpp watchpoint.cpp
//            ../cpu/cpu.cpp ../memory/memory.cpp test_expr.cpp -o test_expr
#include "expr.h"
#include "watchpoint.h"
#include <cstdio>
#include <cassert>

struct TestCase {
    const char *expr;
    uint32_t    expected;
};

static TestCase cases[] = {
    {"1",              1},
    {"1+2",            3},
    {"2*3+4",          10},
    {"2*(3+4)",        14},
    {"10-3-2",         5},
    {"0x10",           16},
    {"0xff&0x0f",      0},    // & 未实现，期望失败——删除此条
    {"1==1",           1},
    {"1!=1",           0},
    {"1+2==3&&4-1==3", 1},
    {"(1+2)*3",        9},
    {"10/2",           5},
};
static const int NR_CASES = sizeof(cases) / sizeof(cases[0]);

int main() {
    int pass = 0, fail = 0;
    for (int i = 0; i < NR_CASES; i++) {
        bool ok;
        uint32_t got = expr_eval(cases[i].expr, &ok);
        if (ok && got == cases[i].expected) {
            printf("PASS  %-30s => %u\n", cases[i].expr, got);
            pass++;
        } else {
            printf("FAIL  %-30s => got %u (ok=%d), expected %u\n",
                   cases[i].expr, got, ok, cases[i].expected);
            fail++;
        }
    }
    printf("\n%d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
```

编译并运行测试：

```bash
cd score/src/monitor
g++ -std=c++17 -I../../include \
    expr.cpp watchpoint.cpp \
    ../cpu/cpu.cpp ../memory/memory.cpp \
    test_expr.cpp -o test_expr
./test_expr
```

期望所有用例 PASS。

---

## 六、本阶段完成检查清单

- [ ] `test_expr` 编译通过
- [ ] 所有测试用例 PASS
- [ ] 负数表达式 `-1` 求值正确（= 0xFFFFFFFF）
- [ ] 括号优先级正确：`2*(3+4)` = 14
- [ ] 十六进制字面量：`0xff` = 255
- [ ] 未知寄存器给出错误提示而不是崩溃
- [ ] 除零给出错误提示而不是崩溃
- [ ] watchpoint 添加/删除/列出功能正常

---

## 七、提交本阶段成果

```bash
git add score/include/expr.h score/include/watchpoint.h \
        score/src/monitor/expr.cpp score/src/monitor/watchpoint.cpp
git commit -m "pa1-b: add expression evaluator and watchpoint module"
git push origin feat/pa1-expr-parser
```

> 不要向 `dev` 合入，等成员 A 完成 PA1-A 后，两人一起进行 [联调1] 再合并。

---

*本阶段完成后，等待成员 A 完成 [PA1-A-SCore调试器骨架]，再进入 [联调1-PA1合并集成]。*
