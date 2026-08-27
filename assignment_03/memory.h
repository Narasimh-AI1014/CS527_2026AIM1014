#ifndef MEMORY_H
#define MEMORY_H

// Global instruction and data memories as per specification
extern char Instruction[256];
extern char Data[4096];

// Function prototypes
void initialize(void);
void finalize(void);

#endif // MEMORY_H
