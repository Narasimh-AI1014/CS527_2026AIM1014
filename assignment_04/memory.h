#ifndef MEMORY_H
#define MEMORY_H

#define NP 4

extern char Instruction[NP][256];
extern char Data[NP][4096];

void initialize(int proc_id, const char *program_filename, const char *data_filename);
void finalize(int proc_id, const char *data_filename);

#endif