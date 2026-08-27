# ====================================================================
# MinGW-Optimized Makefile for Windows / Linux Cross-Compatibility
# ====================================================================

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
TARGET = simulator.exe

SRCS = main.c compiler.c processor.c memory.c
OBJS = $(SRCS:.c=.o)
HEADERS = compiler.h processor.h memory.h

# --- OS Detection Logic for Cross-Platform Compilation ---
ifeq ($(OS),Windows_NT)
    # Windows native cmd configurations
    RM = del /Q /F
    CLEAN_TARGETS = $(OBJS) $(TARGET) program.byte
    FIX_PATH = $(subst /,\,$1)
else
    # Linux/macOS bash configurations
    RM = rm -f
    CLEAN_TARGETS = $(OBJS) $(TARGET) program.byte
    FIX_PATH = $1
endif

all: $(TARGET)

# Link execution blocks
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Compile files into object format
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Safe environment cleaning rule
clean:
	$(RM) $(call FIX_PATH,$(CLEAN_TARGETS))

.PHONY: all clean
