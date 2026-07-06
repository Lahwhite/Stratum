#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <readline/readline.h>
#include <readline/history.h>

#include "../../include/sdb.h"
#include "../../include/cpu.h"
#include "../../include/memory.h"
#include "../../include/expr.h"
#include "../../include/watchpoint.h"

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
    if (args == nullptr) {
        printf("Usage: info r | info w\n");
        return 0;
    }
    if (strncmp(args, "r", 1) == 0) {
        printf("%-8s 0x%08x\n", "pc", cpu.pc);
        for (int i = 0; i < 32; i++) {
            printf("%-8s 0x%08x", GPR_NAMES[i], cpu.gpr[i]);
            if (i % 4 == 3) printf("\n");
            else            printf("  ");
        }
        return 0;
    }
    if (strncmp(args, "w", 1) == 0) {
        wp_print_all();
        return 0;
    }
    printf("Usage: info r | info w\n");
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

static int cmd_p(char *args) {
    if (args == nullptr) {
        printf("Usage: p EXPR\n");
        return 0;
    }
    bool ok;
    uint32_t val = expr_eval(args, &ok);
    if (ok) printf("= 0x%08x  (%u)\n", val, val);
    return 0;
}

static int cmd_w(char *args) {
    if (args == nullptr) {
        printf("Usage: w EXPR\n");
        return 0;
    }
    wp_add(args);
    return 0;
}

static int cmd_d(char *args) {
    if (args == nullptr) {
        printf("Usage: d N\n");
        return 0;
    }
    int id = atoi(args);
    if (wp_delete(id)) printf("Watchpoint %d 已删除\n", id);
    else printf("找不到编号为 %d 的 watchpoint\n", id);
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
    {"help",    "打印本帮助信息",                             cmd_help},
    {"q",       "退出 SDB",                                  nullptr },
    {"si",      "si [N]  单步执行 N 条指令(默认 1)",           cmd_si  },
    {"info",    "info r  打印寄存器状态",                     cmd_info},
    {"x",       "x N ADDR  查看从 ADDR 起 N 个字的内存",       cmd_x   },
    {"p",       "p EXPR  对表达式求值并打印",                  cmd_p   },
    {"w",       "w EXPR  添加 watchpoint",                   cmd_w   },
    {"d",       "d N  删除编号为 N 的 watchpoint",            cmd_d   },
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