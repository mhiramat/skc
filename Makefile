CC = gcc
CLFAGS = -I. -Wall -O2 -g
DEPENDS = compat.h skc.h
OBJ = main.o skc.o

%.o: %.c $(DEPENDS)
	$(CC) -c -o $@ $< $(CFLAGS)

skc: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(OBJ) skc
