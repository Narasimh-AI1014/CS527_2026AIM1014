#include "processor.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>

int Register[256];
int VectorRegister[32][8];

int PC = 0;
int opcode = 0, dest = 0, src1 = 0, src2 = 0;
int end_of_simulation = 0;
int N = 0, Z = 0, C = 0, V = 0;

void reset(void) {
    memset(Register, 0, sizeof(Register));
    memset(VectorRegister, 0, sizeof(VectorRegister));
    PC = 0;
    opcode = 0; dest = 0; src1 = 0; src2 = 0;
    end_of_simulation = 0;
    N = 0; Z = 0; C = 0; V = 0;
}

void fetch(void) {
    // Read 4 bytes from current PC location in Instruction array
    opcode = (unsigned char)Instruction[PC];
    dest   = (unsigned char)Instruction[PC + 1];
    src1   = (unsigned char)Instruction[PC + 2];
    src2   = (unsigned char)Instruction[PC + 3];
    PC += 4;
}

void decode(void) {
    // Left empty deliberately per specification constraints
}

// Helper to safely pull 32-bit word structures from byte addressing lines
static int read_word(int byte_addr) {
    unsigned char b0 = Data[byte_addr];
    unsigned char b1 = Data[byte_addr + 1];
    unsigned char b2 = Data[byte_addr + 2];
    unsigned char b3 = Data[byte_addr + 3];
    return (int)((b3 << 24) | (b2 << 16) | (b1 << 8) | b0);
}

// Helper to write a 32-bit integer back to byte addressing fields
static void write_word(int byte_addr, int value) {
    Data[byte_addr]     = (char)(value & 0xFF);
    Data[byte_addr + 1] = (char)((value >> 8) & 0xFF);
    Data[byte_addr + 2] = (char)((value >> 16) & 0xFF);
    Data[byte_addr + 3] = (char)((value >> 24) & 0xFF);
}

void execute(void) {
    if (opcode == 0) {
        end_of_simulation = 1;
        return;
    }

    // Branch Operations (0x10 to 0x1E)
    if (opcode >= 0x10 && opcode <= 0x1E) {
        int condition_met = 0;
        int cond = opcode & 0x0F;
        switch(cond) {
            case 0: condition_met = (Z == 1); break; // EQ
            case 1: condition_met = (Z == 0); break; // NE
            case 10: condition_met = (N == V); break; // GE
            case 14: condition_met = 1; break;        // AL (Always)
            // Add custom flags mapping options if needed
        }
        if (condition_met) {
            signed char offset = (signed char)src2;
            PC += (offset * 4) - 4; // Adjust because fetch already advanced PC by 4
        }
        return;
    }

    long long res = 0;
    switch(opcode) {
        // --- Scalar Integer Block ---
        case 0x01: // Add Var
            res = (long long)Register[src1] + Register[src2];
            Register[dest] = (int)res;
            break;
        case 0x09: // Add Const
            res = (long long)Register[src1] + src2;
            Register[dest] = (int)res;
            break;
        case 0x02: // Sub Var
            res = (long long)Register[src1] - Register[src2];
            Register[dest] = (int)res;
            break;
        case 0x0A: // Sub Const
            res = (long long)Register[src1] - src2;
            Register[dest] = (int)res;
            break;
        case 0x05: // Load from pointer register
            Register[dest] = read_word(Register[src2]);
            break;
        case 0x06: // Store to pointer register
            write_word(Register[dest], Register[src2]);
            break;
        case 0x07: // Move Const
            Register[dest] = src2;
            break;

        // --- Vector Processing Block ---
        case 0x21: // Vector Add Vector
            for(int i=0; i<8; i++) {
                VectorRegister[dest][i] = VectorRegister[src1][i] + VectorRegister[src2][i];
            }
            break;
        case 0x29: // Vector Add Constant
            for(int i=0; i<8; i++) {
                VectorRegister[dest][i] = VectorRegister[src1][i] + src2;
            }
            break;
        case 0x22: // Vector Sub Vector
            for(int i=0; i<8; i++) {
                VectorRegister[dest][i] = VectorRegister[src1][i] - VectorRegister[src2][i];
            }
            break;
        case 0x23: // Vector Mul Vector / Scalar Register
            // Note: src2 can be an integer register (x) or vector register (v)
            for(int i=0; i<8; i++) {
                VectorRegister[dest][i] = VectorRegister[src1][i] * VectorRegister[src2][i];
            }
            break;
        case 0x2B: // Vector Mul Constant
            for(int i=0; i<8; i++) {
                VectorRegister[dest][i] = VectorRegister[src1][i] * src2;
            }
            break;
        case 0x25: // Vector Load (v1 = [x2])
            {
                int base_addr = Register[src2];
                for(int i=0; i<8; i++) {
                    VectorRegister[dest][i] = read_word(base_addr + (i * 4));
                }
            }
            break;
        case 0x2C: // Vector Load Constant Address (v1 =)
            {
                int base_addr = src2;
                for(int i=0; i<8; i++) {
                    VectorRegister[dest][i] = read_word(base_addr + (i * 4));
                }
            }
            break;
        case 0x26: // Vector Store ([x2] = v1)
            {
                int base_addr = Register[dest];
                for(int i=0; i<8; i++) {
                    write_word(base_addr + (i * 4), VectorRegister[src2][i]);
                }
            }
            break;
    }

    // Flag generation updates for arithmetic loops
    if (opcode == 0x01 || opcode == 0x09 || opcode == 0x02 || opcode == 0x0A) {
        Z = (Register[dest] == 0);
        N = (Register[dest] < 0);
    }
}
