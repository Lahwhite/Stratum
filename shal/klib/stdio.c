#include "../include/klib.h"
#include "../include/am.h"
static int int2str(int val, char *buf, int base)  //处理 %d
{
    char* int2str_r = "0123456789abcdef";
    char temp[32];
    int sign = 0;

    if(val == 0)
    {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    if(val < 0)
    {
        val = -val;
        sign = 1;
    }

    int n = 0;
    while(val != 0)
    {
        temp[n++] = int2str_r[val % base];
        val /= base;
    }

    if(sign)
    {
        buf[0] = '-';
        for(int i=n - 1; i >= 0; i--)
        {
            buf[n-i] = temp[i];
        }
        buf[n+1] = '\0';
        return n+1;
    }
    else
    {
        for(int i = n - 1; i >= 0; i--)
        {
            buf[n-1 - i] = temp[i];
        }
        buf[n] = '\0';
        return n;
    }
    
}

static int uint2str(unsigned int val, char *buf, int base) //处理 %u, %x
{
    char* int2str_r = "0123456789abcdef";
    char temp[32];

    if(val == 0)
    {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    int n = 0;
    while(val != 0)
    {
        temp[n++] = int2str_r[val % base];
        val /= base;
    }

    for(int i = n - 1; i >= 0; i--)
    {
        buf[n - 1 - i] = temp[i];
    }
    buf[n] = '\0';
    return n;
}

int vsprintf(char *out, const char *fmt, va_list ap)
{
    char *o = out;

    while(*fmt)
    {
        if(*fmt != '%')  *o++ = *fmt++;
        else
        {
            fmt++;
            switch(*fmt)
            {
                case 'd':
                {
                    int val = va_arg(ap, int);
                    o += int2str(val, o, 10);
                    break;
                }
                case 'u':
                {
                    unsigned int val = va_arg(ap, int);
                    o += uint2str(val, o, 10);
                    break;
                }
                case 'x':
                {
                    unsigned int val = va_arg(ap, int);
                    o += uint2str(val, o, 16);
                    break;
                }
                case 's':
                {
                    char *str = va_arg(ap, char*);
                    while(*str) *o++ = *str++;
                    break;
                }
                case 'c':
                {
                    *o++ = (char)va_arg(ap, int);
                    break;
                }
                case '%':
                {
                    *o++ = '%';
                    break;
                }
                default:
                {
                    *o++ = '%';
                    *o++ = *fmt;
                    break;
                }
            }
            fmt++;
        }
    }
    *o = '\0';
    return (int)(o - out);
}

int sprintf(char *out, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsprintf(out, fmt, ap);
    va_end(ap);
    return ret;
}

int printf(const char *fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    int ret = vsprintf(buf, fmt, ap);
    va_end(ap);
    for (int i = 0; buf[i]; i++) putch(buf[i]);
    return ret;
}

int puts(const char *s) {
    while(*s) putch(*s++);
    putch('\n');
    return 0;
}