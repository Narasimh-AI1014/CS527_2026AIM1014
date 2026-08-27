#include <stdio.h>
#include <stdlib.h>
#include "memory.h"

char Instruction[NP][256];
char Data[NP][4096];

static void read_hex_file(const char *fname, char *mem, int max_len) {
    FILE *fp = fopen(fname, "r");
    if (!fp) {
        perror("fopen hex file");
        exit(EXIT_FAILURE);
    }
    int val;
    int idx = 0;
    while (fscanf(fp, "%x", &val) == 1 && idx < max_len) {
        mem[idx++] = (char)val;
    }
    fclose(fp);
    while (idx < max_len) mem[idx++] = 0;
}

void initialize(int proc_id, const char *program_filename, const char *data_filename) {
    // Clear both memories
    for (int i = 0; i < 256; i++) Instruction[proc_id][i] = 0;
    for (int i = 0; i < 4096; i++) Data[proc_id][i] = 0;

    // Program file must exist; read it
    read_hex_file(program_filename, Instruction[proc_id], 256);

    // Data file may not exist; if it doesn't, leave data as zeros.
    FILE *fp = fopen(data_filename, "r");
    if (fp) {
        int val;
        int idx = 0;
        while (fscanf(fp, "%x", &val) == 1 && idx < 4096) {
            Data[proc_id][idx++] = (char)val;
        }
        fclose(fp);
        while (idx < 4096) Data[proc_id][idx++] = 0;
    }
    // else: Data remains all zeros, which is acceptable.
}

void finalize(int proc_id, const char *data_filename) {
    FILE *fp = fopen(data_filename, "w");
    if (!fp) {
        perror("fopen data output");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < 4096; i++) {
        fprintf(fp, "%02X", (unsigned char)Data[proc_id][i]);
        if ((i + 1) % 4 == 0)
            fprintf(fp, "\n");
        else
            fprintf(fp, " ");
    }
    fclose(fp);
}