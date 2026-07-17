#include "../include/klib.h"

size_t strlen(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

char *strcpy(char *dst, const char *src)
{
    char *p = dst;
    while ((*p++ = *src++));
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
    {
        dst[i] = src[i];
    }
    for (; i < n; i++)
    {
        dst[i] = '\0';
    }
    return dst;
}

char  *strcat(char *dst, const char *src)
{
    char *p = dst;
    while(*p)p++;
    while((*p++ = *src++));
    return dst;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s1 == *s2)
    {
        s1++;
        s2++;
    }
    return (*(unsigned char*)s1 - *(unsigned char*)s2);
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    if(n==0) return 0;
    while (--n && *s1 && *s1 == *s2)
    {
        s1++;
        s2++;
    }
    return (*(unsigned char*)s1 - *(unsigned char*)s2);
}

char *strchr(const char *s, int c)
{
    unsigned char ch = (unsigned char)c;
    while(*s)
    {
        if(*s == ch) return (char*)s;
        s++;
    }
    if(ch == '\0') return (char*)s;
    return NULL;
}

void  *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char*)s;
    unsigned char val = (unsigned char)c;
    while(n--) *p++ = val;
    return s;
}

void  *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char*)dst;
    const unsigned char *s = (const unsigned char*)src;
    while(n--) *d++ = *s++;
    return dst;
}

void  *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char*)dst;
    const unsigned char *s = (const unsigned char*)src;
    if(d<s) 
    {
        while(n--) *d++ = *s++;
    }
    else
    {
        d += n - 1;
        s += n - 1;
        while(n--) *d-- = *s--;
    }
    return dst;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    if(n==0) return 0;
    const unsigned char *p1 = (const unsigned char*)s1;
    const unsigned char *p2 = (const unsigned char*)s2;
    while (--n && *p1 == *p2)
    {
        p1++;
        p2++;
    }
    return (*p1 - *p2);
}