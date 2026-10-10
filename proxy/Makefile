CC = gcc
CFLAGS = -Wall -Wextra -g
LIBS = -lsqlite3

all: proxy

proxy: main.c proxy_parse.c
	$(CC) $(CFLAGS) -o proxy main.c proxy_parse.c $(LIBS)

clean:
	rm -f proxy
