#include "compiler.h"
#include "memory.h"
#include "processor.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <source_assembly_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("[1/5] Compiling assembly program: %s...\n", argv[1]);
    compile(argv[1]);

    printf("[2/5] Initializing instruction and data memories...\n");
    initialize();

    printf("[3/5] Resetting processor core state...\n");
    reset();

    printf("[4/5] Executing simulation pipeline loop...\n");
    while (!end_of_simulation) {
        fetch();
        decode();
        execute();
    }

    printf("[5/5] Saving final data memory workspace states to disk...\n");
    finalize();

    printf("Simulation successfully completed! Results written to data.byte.\n");
    return EXIT_SUCCESS;
}
