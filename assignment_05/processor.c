#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "processor.h"

#ifndef DEBUG_FETCH_TRACE
#define DEBUG_FETCH_TRACE 0
#endif
#ifndef FETCH_DELAY_US
#define FETCH_DELAY_US 1000
#endif

char current_prog_name[256] = "unknown";
int32_t IntReg[NP][NUM_INT_REG];
int32_t VecReg[NP][NUM_VEC_REG][VEC_LEN];
int PC[NP], Flags[NP][4], EndSim[NP];

int current_pid = -1;

static FILE *log_fp[NP];

static void set_flags_add(int proc_id, int32_t a, int32_t b, int32_t res) {
    Flags[proc_id][0] = (res == 0) ? 1 : 0;
    Flags[proc_id][1] = ((uint32_t)res >> 31) & 1;
    Flags[proc_id][2] = ((uint32_t)res < (uint32_t)a) ? 1 : 0;
    int sa = (uint32_t)a >> 31;
    int sb = (uint32_t)b >> 31;
    int sr = (uint32_t)res >> 31;
    Flags[proc_id][3] = (sa == sb && sr != sa) ? 1 : 0;
}

static void set_flags_sub(int proc_id, int32_t a, int32_t b, int32_t res) {
    Flags[proc_id][0] = (res == 0) ? 1 : 0;
    Flags[proc_id][1] = ((uint32_t)res >> 31) & 1;
    Flags[proc_id][2] = ((uint32_t)a > (uint32_t)b) ? 1 : 0;
    int sa = (uint32_t)a >> 31;
    int sb = (uint32_t)b >> 31;
    int sr = (uint32_t)res >> 31;
    Flags[proc_id][3] = (sa != sb && sr == sb) ? 1 : 0;
}

void reset(int proc_id) {
    for (int i = 0; i < NUM_INT_REG; i++)
        IntReg[proc_id][i] = 0;
    for (int i = 0; i < NUM_VEC_REG; i++)
        for (int j = 0; j < VEC_LEN; j++)
            VecReg[proc_id][i][j] = 0;

    PC[proc_id] = 0;
    Flags[proc_id][0] = Flags[proc_id][1] =
    Flags[proc_id][2] = Flags[proc_id][3] = 0;
    EndSim[proc_id] = 0;

    char logname[64];
    snprintf(logname, sizeof(logname), "logs/process_%d.log", proc_id);
    if (log_fp[proc_id]) fclose(log_fp[proc_id]);
    log_fp[proc_id] = fopen(logname, "a");
    if (!log_fp[proc_id]) {
        perror("fopen log");
        exit(EXIT_FAILURE);
    }
}

void close_log(int proc_id) {
    if (log_fp[proc_id]) {
        fclose(log_fp[proc_id]);
        log_fp[proc_id] = NULL;
    }
}

static int opcode, dest, src1, src2;

void fetch(int proc_id) {
#if FETCH_DELAY_US > 0
    usleep(FETCH_DELAY_US);
#endif
    if (PC[proc_id] + 3 >= 1024) {
        EndSim[proc_id] = 1;
        return;
    }
    int phys = getPhysicallAddress(proc_id, 1, PC[proc_id]);
    opcode = (uint8_t)read_byte(phys);
    dest   = (uint8_t)read_byte(phys + 1);
    src1   = (uint8_t)read_byte(phys + 2);
    src2   = (uint8_t)read_byte(phys + 3);

#if DEBUG_FETCH_TRACE
    printf("%s: %02X %02X %02X %02X\n", current_prog_name, opcode, dest, src1, src2);
#endif

    PC[proc_id] += 4;
}

void decode(void) {
    /* direct-execute design */
}

/* Pre-switch bounds check for vector-register indices. */
static int vec_regs_in_range(void) {
    switch (opcode) {
        case 0x21: case 0x22: case 0x23:
            return dest < NUM_VEC_REG && src1 < NUM_VEC_REG && src2 < NUM_VEC_REG;
        case 0x29: case 0x2A: case 0x2B:
        case 0x31: case 0x32: case 0x33:
            return dest < NUM_VEC_REG && src1 < NUM_VEC_REG;
        case 0x25: case 0x2C: case 0x2E:
            return dest < NUM_VEC_REG;
        case 0x26:
            return src2 < NUM_VEC_REG;
        default:
            return 1;
    }
}

/* Validate a logical word address: 4-aligned, and last byte < 4096. */
static int valid_word_addr(int proc_id, int addr) {
    if (addr < 0 || (addr & 3) != 0 || addr > 4092) {
        fprintf(stderr, "Invalid word address %d (must be 4-aligned, 0..4092)\n", addr);
        EndSim[proc_id] = 1;
        return 0;
    }
    return 1;
}

void execute(int proc_id) {
    if (!vec_regs_in_range()) {
        fprintf(stderr, "Vector register index out of bounds (opcode 0x%02X)\n", opcode);
        EndSim[proc_id] = 1;
        return;
    }

    switch (opcode) {
        case 0:
            EndSim[proc_id] = 1;
            break;

        case 0x01: {
            int32_t a = IntReg[proc_id][src1];
            int32_t b = IntReg[proc_id][src2];
            int32_t r = a + b;
            IntReg[proc_id][dest] = r;
            set_flags_add(proc_id, a, b, r);
            break;
        }
        case 0x09: {
            int32_t a = IntReg[proc_id][src1];
            int32_t b = src2;
            int32_t r = a + b;
            IntReg[proc_id][dest] = r;
            set_flags_add(proc_id, a, b, r);
            break;
        }

        case 0x02: {
            int32_t a = IntReg[proc_id][src1];
            int32_t b = IntReg[proc_id][src2];
            int32_t r = a - b;
            IntReg[proc_id][dest] = r;
            set_flags_sub(proc_id, a, b, r);
            break;
        }
        case 0x0A: {
            int32_t a = IntReg[proc_id][src1];
            int32_t b = src2;
            int32_t r = a - b;
            IntReg[proc_id][dest] = r;
            set_flags_sub(proc_id, a, b, r);
            break;
        }

        case 0x03:
            IntReg[proc_id][dest] = IntReg[proc_id][src1] * IntReg[proc_id][src2];
            break;
        case 0x0B:
            IntReg[proc_id][dest] = IntReg[proc_id][src1] * src2;
            break;

        case 0x04:
            IntReg[proc_id][dest] = (IntReg[proc_id][src2] != 0)
                ? IntReg[proc_id][src1] / IntReg[proc_id][src2] : 0;
            break;
        case 0x0C:
            IntReg[proc_id][dest] = (src2 != 0)
                ? IntReg[proc_id][src1] / src2 : 0;
            break;

        case 0x05: {
            int addr = IntReg[proc_id][src2];
            if (!valid_word_addr(proc_id, addr)) break;
            int phys = getPhysicallAddress(proc_id, 0, addr);
            IntReg[proc_id][dest] = read_word(phys);
            break;
        }
        case 0x0D: {
            int addr = src2;
            if (!valid_word_addr(proc_id, addr)) break;
            int phys = getPhysicallAddress(proc_id, 0, addr);
            IntReg[proc_id][dest] = read_word(phys);
            break;
        }

        case 0x06: {
            int addr = IntReg[proc_id][dest];
            if (!valid_word_addr(proc_id, addr)) break;
            int phys = getPhysicallAddress(proc_id, 0, addr);
            write_word(phys, IntReg[proc_id][src2]);
            break;
        }
        case 0x0E: {
            int addr = src2;
            if (!valid_word_addr(proc_id, addr)) break;
            int phys = getPhysicallAddress(proc_id, 0, addr);
            write_word(phys, IntReg[proc_id][dest]);
            break;
        }

        case 0x07:
            IntReg[proc_id][dest] = IntReg[proc_id][src2];
            break;
        case 0x0F:
            IntReg[proc_id][dest] = src2;
            break;

        case 0x08: {
            time_t now = time(NULL);
            char timestamp[64];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
            fprintf(log_fp[proc_id], "[%s] Process id: %d x%d : %08X\n",
                    timestamp, current_pid, src2, IntReg[proc_id][src2]);
            fflush(log_fp[proc_id]);
            break;
        }

        case 0x10: case 0x11: case 0x12: case 0x13:
        case 0x14: case 0x15: case 0x16: case 0x17:
        case 0x18: case 0x19: case 0x1A: case 0x1B:
        case 0x1C: case 0x1D: case 0x1E: {
            int Z = Flags[proc_id][0], N = Flags[proc_id][1],
                C = Flags[proc_id][2], V = Flags[proc_id][3];
            int cond = 0;
            switch (opcode - 0x10) {
                case 0x0: cond = Z; break;
                case 0x1: cond = !Z; break;
                case 0x2: cond = C; break;
                case 0x3: cond = !C; break;
                case 0x4: cond = N; break;
                case 0x5: cond = !N; break;
                case 0x6: cond = V; break;
                case 0x7: cond = !V; break;
                case 0x8: cond = C && !Z; break;
                case 0x9: cond = !C || Z; break;
                case 0xA: cond = (N == V); break;
                case 0xB: cond = (N != V); break;
                case 0xC: cond = !Z && (N == V); break;
                case 0xD: cond = Z || (N != V); break;
                case 0xE: cond = 1; break;
            }
            if (cond) {
                int offset = (int8_t)src2;
                PC[proc_id] = (PC[proc_id] - 4) + offset * 4;
            }
            break;
        }

        case 0x21:
            for (int i = 0; i < VEC_LEN; i++)
                VecReg[proc_id][dest][i] = VecReg[proc_id][src1][i] +
                                           VecReg[proc_id][src2][i];
            break;
        case 0x29:
            for (int i = 0; i < VEC_LEN; i++)
                VecReg[proc_id][dest][i] = VecReg[proc_id][src1][i] + src2;
            break;
        case 0x31:
            for (int i = 0; i < VEC_LEN; i++)
                VecReg[proc_id][dest][i] = VecReg[proc_id][src1][i] +
                                           IntReg[proc_id][src2];
            break;

        case 0x22:
            for (int i = 0; i < VEC_LEN; i++)
                VecReg[proc_id][dest][i] = VecReg[proc_id][src1][i] -
                                           VecReg[proc_id][src2][i];
            break;
        case 0x2A:
            for (int i = 0; i < VEC_LEN; i++)
                VecReg[proc_id][dest][i] = VecReg[proc_id][src1][i] - src2;
            break;
        case 0x32:
            for (int i = 0; i < VEC_LEN; i++)
                VecReg[proc_id][dest][i] = VecReg[proc_id][src1][i] -
                                           IntReg[proc_id][src2];
            break;

        case 0x23:
            for (int i = 0; i < VEC_LEN; i++)
                VecReg[proc_id][dest][i] = VecReg[proc_id][src1][i] *
                                           VecReg[proc_id][src2][i];
            break;
        case 0x2B:
            for (int i = 0; i < VEC_LEN; i++)
                VecReg[proc_id][dest][i] = VecReg[proc_id][src1][i] * src2;
            break;
        case 0x33:
            for (int i = 0; i < VEC_LEN; i++)
                VecReg[proc_id][dest][i] = VecReg[proc_id][src1][i] *
                                           IntReg[proc_id][src2];
            break;

        case 0x25: {
            int base = IntReg[proc_id][src2];
            if (!valid_word_addr(proc_id, base) ||
                !valid_word_addr(proc_id, base + (VEC_LEN - 1) * 4)) break;
            for (int i = 0; i < VEC_LEN; i++) {
                int phys = getPhysicallAddress(proc_id, 0, base + i * 4);
                VecReg[proc_id][dest][i] = read_word(phys);
            }
            break;
        }
        case 0x2C: {
            int base = src2;
            if (!valid_word_addr(proc_id, base) ||
                !valid_word_addr(proc_id, base + (VEC_LEN - 1) * 4)) break;
            for (int i = 0; i < VEC_LEN; i++) {
                int phys = getPhysicallAddress(proc_id, 0, base + i * 4);
                VecReg[proc_id][dest][i] = read_word(phys);
            }
            break;
        }

        case 0x26: {
            int base = IntReg[proc_id][dest];
            if (!valid_word_addr(proc_id, base) ||
                !valid_word_addr(proc_id, base + (VEC_LEN - 1) * 4)) break;
            for (int i = 0; i < VEC_LEN; i++) {
                int phys = getPhysicallAddress(proc_id, 0, base + i * 4);
                write_word(phys, VecReg[proc_id][src2][i]);
            }
            break;
        }
        case 0x2E: {
            int base = src2;
            if (!valid_word_addr(proc_id, base) ||
                !valid_word_addr(proc_id, base + (VEC_LEN - 1) * 4)) break;
            for (int i = 0; i < VEC_LEN; i++) {
                int phys = getPhysicallAddress(proc_id, 0, base + i * 4);
                write_word(phys, VecReg[proc_id][dest][i]);
            }
            break;
        }

        default:
            break;
    }

    /* Enforce x0 == 0 regardless of what the instruction did. */
    IntReg[proc_id][0] = 0;
}

void process_instructions(int proc_id, int pid, int instruction_count) {
    current_pid = pid;
    for (int i = 0; i < instruction_count && !EndSim[proc_id]; i++) {
        fetch(proc_id);
        decode();
        execute(proc_id);
    }
    usleep(10);
}