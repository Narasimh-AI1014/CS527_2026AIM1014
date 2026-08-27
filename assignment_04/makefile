CC = gcc
CFLAGS = -Wall -Wextra -g
OBJS = main.o compiler.o processor.o memory.o os.o

all: sim

sim: $(OBJS)
	$(CC) $(CFLAGS) -o sim $(OBJS)

main.o: main.c os.h
compiler.o: compiler.c compiler.h
processor.o: processor.c processor.h
memory.o: memory.c memory.h
os.o: os.c os.h compiler.h processor.h memory.h

clean:
	rm -f *.o sim program.byte proc_*.log data_out_*.byte