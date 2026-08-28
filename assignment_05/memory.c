#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "memory.h"

char memory[MEMSIZE];   // Physical memory

char read_byte(int phys_addr) {
    if (phys_addr < 0 || phys_addr >= MEMSIZE) {
        fprintf(stderr, "Physical memory read out of bounds: %d\n", phys_addr);
        exit(EXIT_FAILURE);
    }
    return memory[phys_addr];
}

void write_byte(int phys_addr, char value) {
    if (phys_addr < 0 || phys_addr >= MEMSIZE) {
        fprintf(stderr, "Physical memory write out of bounds: %d\n", phys_addr);
        exit(EXIT_FAILURE);
    }
    memory[phys_addr] = value;
}

int32_t read_word(int phys_addr) {
    if (phys_addr < 0 || phys_addr + 3 >= MEMSIZE) {
        fprintf(stderr, "Physical memory word read out of bounds: %d\n", phys_addr);
        exit(EXIT_FAILURE);
    }
    uint8_t b0 = (uint8_t)memory[phys_addr];
    uint8_t b1 = (uint8_t)memory[phys_addr + 1];
    uint8_t b2 = (uint8_t)memory[phys_addr + 2];
    uint8_t b3 = (uint8_t)memory[phys_addr + 3];
    return (int32_t)((uint32_t)b0 |
                     ((uint32_t)b1 << 8) |
                     ((uint32_t)b2 << 16) |
                     ((uint32_t)b3 << 24));
}

void write_word(int phys_addr, int32_t value) {
    if (phys_addr < 0 || phys_addr + 3 >= MEMSIZE) {
        fprintf(stderr, "Physical memory word write out of bounds: %d\n", phys_addr);
        exit(EXIT_FAILURE);
    }
    memory[phys_addr] = (char)(value & 0xFF);
    memory[phys_addr + 1] = (char)((value >> 8) & 0xFF);
    memory[phys_addr + 2] = (char)((value >> 16) & 0xFF);
    memory[phys_addr + 3] = (char)((value >> 24) & 0xFF);
}