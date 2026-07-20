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
