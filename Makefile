CC = clang
LD = ld
lib = -llua -ledit
bin = bin/scs
src = src/main.c

all: $(src)
	$(CC) $(src) -o $(bin) $(lib); 

install: all
	cp $(bin) /$(bin)
