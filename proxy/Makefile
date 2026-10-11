CC = gcc
CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200112L -Wall -Wextra -g
LIBS = -lsqlite3

all: proxy

proxy: main.c
	$(CC) $(CFLAGS) -o proxy main.c $(LIBS) -pthread

clean:
	rm -f proxy
