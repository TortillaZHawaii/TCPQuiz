CC=gcc
CFLAGS= -std=gnu99 -Wall
PROGRAMS=server client

all: ${PROGRAMS}

server: server.c
	${CC} ${CFLAGS} server.c -o server

client: client.c
	${CC} ${CFLAGS} client.c -o client

clean:
	rm ${PROGRAMS}
