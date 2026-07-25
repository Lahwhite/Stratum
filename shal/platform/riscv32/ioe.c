#include <am.h>

#define UART_TX  (*(volatile char *)0xa0000000)

void putch(char c) {
    UART_TX = c;   // 向 UART TX 地址写一个字节
}