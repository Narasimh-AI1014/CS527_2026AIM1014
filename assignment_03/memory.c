#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global array allocations
char Instruction[256];
char Data[4096];

// Helper function to safely read hex byte lines into a memory array
static void load_hex_file(const char *filename, char *memory_array, size_t max_size) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        // If file doesn't exist, leave memory uninitialized (all zeros)
        return;
    }

    unsigned int b1, b2, b3, b4;
    size_t index = 0;

    // Scan four space-separated hex bytes per line
    while (fscanf(file, "%X %X %X %X", &b1, &b2, &b3, &b4) == 4) {
        if (index + 4 > max_size) break;
        
        memory_array[index++] = (char)(b1 & 0xFF);
        memory_array[index++] = (char)(b2 & 0xFF);
        memory_array[index++] = (char)(b3 & 0xFF);
        memory_array[index++] = (char)(b4 & 0xFF);
    }

    fclose(file);
}

// Reads data from program.byte and data.byte to populate simulation memory
void initialize(void) {
    // Clear memories first to guarantee deterministic resetting
    memset(Instruction, 0, sizeof(Instruction));
    memset(Data, 0, sizeof(Data));

    load_hex_file("program.byte", Instruction, sizeof(Instruction));
    load_hex_file("data.byte", Data, sizeof(Data));
}

// Writes the final modified Data memory back to data.byte in hex space-separated lines
void finalize(void) {
    FILE *file = fopen("data.byte", "w");
    if (!file) {
        perror("Error creating data.byte during finalization");
        return;
    }

    // Write 4 bytes per line in uppercase hexadecimal formatting
    for (size_t i = 0; i < sizeof(Data); i += 4) {
        fprintf(file, "%02X %02X %02X %02X\n",
                (unsigned char)Data[i],
                (unsigned char)Data[i + 1],
                (unsigned char)Data[i + 2],
                (unsigned char)Data[i + 3]);
    }

    fclose(file);
}
