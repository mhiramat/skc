CC = gcc
CFLAGS = -I./include -Wall -O2 -g
DEPENDS = include/linux/skc.h
OBJ = main.o skc.o skced.o

%.o: %.c $(DEPENDS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: skc skced

skc: main.o skc.o
	$(CC) -o $@ $^ $(CFLAGS)

skced: skced.o skc.o
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(OBJ) skc skced
