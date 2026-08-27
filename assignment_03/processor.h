#ifndef PROCESSOR_H
#define PROCESSOR_H

// Simulation Engine structural variables
extern int Register[256];
extern int VectorRegister[32][8]; // Added 32 vector registers of 8 items each

extern int PC, opcode, dest, src1, src2;
extern int end_of_simulation;

// Global Flag Bits
extern int N, Z, C, V;

// Core Architectural Functions
void reset(void);
void fetch(void);
void decode(void);
void execute(void);

#endif // PROCESSOR_H
