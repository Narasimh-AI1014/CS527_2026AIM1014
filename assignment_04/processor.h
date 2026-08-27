#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <stdint.h>

#define NP 4
#define NUM_INT_REG 256
#define NUM_VEC_REG 32
#define VEC_LEN 8

extern int32_t IntReg[NP][NUM_INT_REG];
extern int32_t VecReg[NP][NUM_VEC_REG][VEC_LEN];
extern int PC[NP], Flags[NP][4], EndSim[NP];
extern char Instruction[NP][256];
extern char Data[NP][4096];
extern int current_pid;

void reset(int proc_id);
void fetch(int proc_id);
void decode(void);
void execute(int proc_id);
void process_instructions(int proc_id, int pid, int instruction_count);

#endif