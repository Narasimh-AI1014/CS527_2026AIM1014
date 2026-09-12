#ifndef COMPILER_H
#define COMPILER_H

#include "processor.h"

/* Returns 0 on success, -1 on failure. On failure, no output file is left. */
int compile(const char *source_filename, const char *prog_filename);

#endif /* COMPILER_H */