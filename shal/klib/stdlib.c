#include "../include/klib.h"

int atoi(const char *s)
{
    while(*s == ' ' || *s=='\t' || *s=='\n') s++;

    int sign = 1;
    if(*s == '-')
    {
        sign = -1;
        s++;
    }
    else if(*s == '+')
    {
        s++;
    }

    int result = 0;
    while(*s >= '0' && *s <= '9')
    {
        result = result * 10 + (*s - '0');
        s++;
    }
    return result * sign;
}

char *itoa(int value, char *str, int base) {
    // 支持的进制：2 ~ 16
    char *digits = "0123456789abcdef";
    char temp[33];   // 32 位 int 最多 32 位二进制 + 符号位
    int i = 0;
    int sign = 0;

    // 处理零
    if (value == 0) {
        str[0] = '0';
        str[1] = '\0';
        return str;
    }

    // 处理负数（仅当 base == 10 时支持负号）
    if (value < 0 && base == 10) {
        sign = 1;
        value = -value;
    }

    // 逐位取出（倒序）
    while (value != 0) {
        temp[i++] = digits[value % base];
        value /= base;
    }

    // 添加符号
    int idx = 0;
    if (sign) str[idx++] = '-';

    // 反转 temp 到 str
    while (i > 0) {
        str[idx++] = temp[--i];
    }
    str[idx] = '\0';
    return str;
}


// ==================== malloc / free ====================
static uint8_t heap[4 * 1024 * 1024];  // 4MB 静态堆
static size_t  heap_used = 0;

void *malloc(size_t size) {
    // 对齐到 8 字节
    size = (size + 7) & ~7u;
    if (heap_used + size > sizeof(heap)) return NULL;
    void *p = heap + heap_used;
    heap_used += size;
    return p;
}

void free(void *p) {
    // 极简版本：不释放，只用于不需要真正回收的场景
    (void)p;
}