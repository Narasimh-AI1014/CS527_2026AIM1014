#ifndef COMPILER_H
#define COMPILER_H

#define MAX_LABELS 256

// Keep the structure definition strictly inside the header file
typedef struct {
    char name[64];
    int address;
} Label;

// Function prototype
void compile(const char *source_filename);

#endif // COMPILER_H
