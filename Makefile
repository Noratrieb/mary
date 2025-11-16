CC=clang
CFLAGS=-std=c23 -g -Wall -Wpedantic -fsanitize=undefined

all: mary

mary:

clean:
	rm mary
