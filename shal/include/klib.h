#pragma once

#include<stdarg.h>
#include "klib-macros.h"

//自定义类型
typedef __SIZE_TYPE__ size_t;
typedef unsigned char uint8_t;

// string.h 子集
size_t strlen(const char *s);
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
char  *strcat(char *dst, const char *src);
int    strcmp(const char *s1, const char *s2);
int    strncmp(const char *s1, const char *s2, size_t n);
char  *strchr(const char *s, int c);

// string.h 的内存函数
void  *memset(void *s, int c, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    memcmp(const void *s1, const void *s2, size_t n);

// ----- stdio.h 子集 -----
int printf(const char *fmt, ...);
int sprintf(char *out, const char *fmt, ...);
int vsprintf(char *out, const char *fmt, va_list ap);
int puts(const char *s);

// ----- stdlib.h 子集 -----
int atoi(const char *s);
char *itoa(int value, char *str, int base);
void *malloc(size_t size);
void free(void *p);