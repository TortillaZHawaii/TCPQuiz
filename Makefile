CC=gcc
CFLAGS= -std=gnu99 -Wall

all: server.c
	${CC} ${CFLAGS} server.c -o server

clean:
	rm server
