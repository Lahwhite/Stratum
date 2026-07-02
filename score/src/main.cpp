#include <cstdio>

#include "../include/sdb.h"

int main(int argc, char *argv[]) {
    printf("Stratum-Core (SCore) - RISC-V 32-bit Simulator\n");
    printf("Build: " __DATE__ " " __TIME__ "\n\n");
    sdb_mainloop();
    return 0;
}