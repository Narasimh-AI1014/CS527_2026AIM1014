#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <stdint.h>
#include "memory.h"   // for MEMSIZE, PAGESIZE, etc.

#define NUM_INT_REG 256
#define NUM_VEC_REG 32
#define VEC_LEN 8
#define NP 4

extern int32_t IntReg[NP][NUM_INT_REG];
extern int32_t VecReg[NP][NUM_VEC_REG][VEC_LEN];
extern int PC[NP], Flags[NP][4], EndSim[NP];
extern int current_pid;
extern char current_prog_name[256];


// OS-provided translation function
int getPhysicallAddress(int proc_id, int isFetch, int address);

void reset(int proc_id);
void fetch(int proc_id);
void decode(void);
void execute(int proc_id);
void process_instructions(int proc_id, int pid, int instruction_count);
void close_log(int proc_id);

#endif