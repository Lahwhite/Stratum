CC     := gcc
CFLAGS := -std=c99 -Wall -I../../include

SRCS := main.c \
        ../../klib/string.c \
        ../../klib/stdio.c  \
        ../../klib/stdlib.c \
        ../../platform/native/ioe.c

hello: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f hello