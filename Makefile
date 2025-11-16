CC=clang
CFLAGS=-std=c23 -g -Wall -Wpedantic -fsanitize=address,undefined

all: mary

mary:

clean:
	rm mary
