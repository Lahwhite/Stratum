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