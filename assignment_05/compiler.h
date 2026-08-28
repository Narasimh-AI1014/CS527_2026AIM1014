#ifndef COMPILER_H
#define COMPILER_H

#include "processor.h"   // for NUM_INT_REG, NUM_VEC_REG

void compile(const char *source_filename, const char *prog_filename);

#endif