// shal/platform/native/ioe.c
#include <unistd.h>  // write()
#include "../../include/am.h"

void putch(char c) {
    write(1, &c, 1);   // 写到 stdout（fd=1）
}