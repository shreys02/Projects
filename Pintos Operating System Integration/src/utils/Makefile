all: setitimer-helper squish-pty squish-unix

CC = gcc
CFLAGS = -Wall -W -Wno-format-security
LOADLIBES = -lm
setitimer-helper: setitimer-helper.o
squish-pty: squish-pty.o
squish-unix: squish-unix.o

clean:
	rm -f *.o setitimer-helper squish-pty squish-unix
