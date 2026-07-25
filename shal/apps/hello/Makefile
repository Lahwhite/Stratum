CC     := gcc
CFLAGS := -std=c99 -Wall -I../../include

SRCS := main.c \
        ../../klib/string.c \
        ../../klib/stdio.c  \
        ../../klib/stdlib.c \
                ../../klib/softdiv.c \
        ../../platform/native/ioe.c

RISCV_CC     := riscv64-unknown-elf-gcc
RISCV_CFLAGS := -march=rv32i -mabi=ilp32 -std=c99 -nostdlib -Wall -I../../include \
                -Wl,-Ttext=0x80000000 -Wl,-N -e main
RISCV_SRCS   := main.c \
                ../../klib/string.c \
                ../../klib/stdio.c  \
                ../../klib/stdlib.c \
                ../../klib/softdiv.c \
                ../../platform/riscv32/ioe.c

hello.elf: $(RISCV_SRCS)
	$(RISCV_CC) $(RISCV_CFLAGS) -o $@ $^

clean:
	rm -f hello
	rm -f hello.elf
