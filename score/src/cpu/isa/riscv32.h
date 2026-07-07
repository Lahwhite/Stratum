#pragma once
#include <cstdint>

// 从 32 位指令中提取字段
#define BITS(x, hi, lo)   (((x) >> (lo)) & ((1u << ((hi)-(lo)+1)) - 1))
#define BIT(x, b)         (((x) >> (b)) & 1u)

// 各类型指令字段提取
#define OPCODE(i)   BITS(i,  6,  0)
#define RD(i)       BITS(i, 11,  7)
#define FUNCT3(i)   BITS(i, 14, 12)
#define RS1(i)      BITS(i, 19, 15)
#define RS2(i)      BITS(i, 24, 20)
#define FUNCT7(i)   BITS(i, 31, 25)

// 立即数提取（已符号扩展）
inline int32_t IMM_I(uint32_t i) {
    return (int32_t)i >> 20;   // bits[31:20]，算术右移做符号扩展
}
inline int32_t IMM_S(uint32_t i) {
    return ((int32_t)(i & 0xfe000000) >> 20) | (int32_t)BITS(i, 11, 7);
}
inline int32_t IMM_B(uint32_t i) {
    return (int32_t)(
        (BIT(i,31) << 12) | (BIT(i,7)  << 11) |
        (BITS(i,30,25) << 5) | (BITS(i,11,8) << 1)
    ) | -(int32_t)(BIT(i,31) << 12);   // 符号扩展
}
inline int32_t IMM_U(uint32_t i) {
    return (int32_t)(i & 0xfffff000u);
}
inline int32_t IMM_J(uint32_t i) {
    return (int32_t)(
        (BIT(i,31) << 20)   | (BITS(i,19,12) << 12) |
        (BIT(i,20) << 11)   | (BITS(i,30,21) << 1)
    ) | -(int32_t)(BIT(i,31) << 20);
}