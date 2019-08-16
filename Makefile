CC = gcc
CFLAGS = -I./include -Wall -O2 -g
DEPENDS = include/linux/skc.h
OBJ = main.o skc.o

%.o: %.c $(DEPENDS)
	$(CC) -c -o $@ $< $(CFLAGS)

skc: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(OBJ) skc
