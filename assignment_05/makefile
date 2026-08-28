CC = gcc
CFLAGS = -Wall -g
OBJS = main.o compiler.o processor.o memory.o os.o
TARGET = simulator

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

main.o: main.c os.h
	$(CC) $(CFLAGS) -c main.c

compiler.o: compiler.c compiler.h processor.h
	$(CC) $(CFLAGS) -c compiler.c

processor.o: processor.c processor.h memory.h
	$(CC) $(CFLAGS) -c processor.c

memory.o: memory.c memory.h
	$(CC) $(CFLAGS) -c memory.c

os.o: os.c os.h compiler.h processor.h memory.h
	$(CC) $(CFLAGS) -c os.c

clean:
	rm -f *.o $(TARGET)

run: $(TARGET)
	./$(TARGET)