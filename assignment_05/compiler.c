#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "compiler.h"

#define MAX_TOKENS 16
#define MAX_LABELS 256

static char *trim(char *s) {
    char *start = s;
    while (isspace((unsigned char)*start)) {
        start++;
    }
    if (*start == '\0') {
        *s = '\0';
        return s;
    }
    memmove(s, start, strlen(start) + 1);
    char *end = s + strlen(s) - 1;
    while (end >= s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

static void strip_comment(char *s) {
    char *p = strchr(s, '%');
    if (p) *p = '\0';
}

static void tokenize(char *line, char tokens[][32], int *n) {
    // Increased buffer to 512 to handle maximum possible expansion
    char temp[512];
    int j = 0;
    for (int i = 0; line[i]; i++) {
        if (line[i] == '[') {
            temp[j++] = ' ';
            temp[j++] = '[';
            temp[j++] = ' ';
        } else if (line[i] == ']') {
            temp[j++] = ' ';
            temp[j++] = ']';
            temp[j++] = ' ';
        } else {
            temp[j++] = line[i];
        }
    }
    temp[j] = '\0';

    *n = 0;
    char *tok = strtok(temp, " \t");
    while (tok && *n < MAX_TOKENS) {
        strcpy(tokens[(*n)++], tok);
        tok = strtok(NULL, " \t");
    }
}

static int parse_reg(const char *s, int *is_vec, int *num) {
    if (s[0] == 'x' || s[0] == 'X') {
        *is_vec = 0;
        *num = atoi(s + 1);
        if (*num < 0 || *num >= NUM_INT_REG) {
            fprintf(stderr, "Invalid integer register: %s\n", s);
            exit(EXIT_FAILURE);
        }
        return 1;
    }
    if (s[0] == 'v' || s[0] == 'V') {
        *is_vec = 1;
        *num = atoi(s + 1);
        if (*num < 0 || *num >= NUM_VEC_REG) {
            fprintf(stderr, "Invalid vector register: %s\n", s);
            exit(EXIT_FAILURE);
        }
        return 1;
    }
    return 0;
}

static int is_number(const char *s) {
    if (*s == '\0') return 0;
    for (const char *p = s; *p; p++)
        if (!isdigit((unsigned char)*p)) return 0;
    return 1;
}

static int check_constant_range(const char *s) {
    int val = atoi(s);
    if (val < 0 || val > 255) {
        fprintf(stderr, "Constant out of range (0-255): %s\n", s);
        exit(EXIT_FAILURE);
    }
    return val;
}

static int branch_code(const char *s) {
    if (strcmp(s, "EQ") == 0) return 0x0;
    if (strcmp(s, "NE") == 0) return 0x1;
    if (strcmp(s, "CS") == 0) return 0x2;
    if (strcmp(s, "CC") == 0) return 0x3;
    if (strcmp(s, "MI") == 0) return 0x4;
    if (strcmp(s, "PL") == 0) return 0x5;
    if (strcmp(s, "VS") == 0) return 0x6;
    if (strcmp(s, "VC") == 0) return 0x7;
    if (strcmp(s, "HI") == 0) return 0x8;
    if (strcmp(s, "LS") == 0) return 0x9;
    if (strcmp(s, "GE") == 0) return 0xA;
    if (strcmp(s, "LT") == 0) return 0xB;
    if (strcmp(s, "GT") == 0) return 0xC;
    if (strcmp(s, "LE") == 0) return 0xD;
    if (strcmp(s, "AL") == 0) return 0xE;
    return -1;
}

void compile(const char *source_filename, const char *prog_filename) {
    FILE *src = fopen(source_filename, "r");
    if (!src) {
        perror("fopen source");
        exit(EXIT_FAILURE);
    }

    char line[256];
    char labels[MAX_LABELS][64];
    int label_addr[MAX_LABELS];
    int label_count = 0;

    // First pass: collect labels
    int instr_index = 0;
    while (fgets(line, sizeof(line), src)) {
        strip_comment(line);
        trim(line);
        if (line[0] == '.') {
            strncpy(labels[label_count], line + 1, 63);
            labels[label_count][63] = '\0';
            label_addr[label_count] = instr_index;
            label_count++;
        } else if (strlen(line) > 0) {
            instr_index++;
        }
    }

    rewind(src);

    FILE *out = fopen(prog_filename, "w");
    if (!out) {
        perror("fopen prog");
        exit(EXIT_FAILURE);
    }

    instr_index = 0;
    while (fgets(line, sizeof(line), src)) {
        strip_comment(line);
        trim(line);
        if (line[0] == '.' || strlen(line) == 0)
            continue;

        char tokens[MAX_TOKENS][32];
        int nt;
        tokenize(line, tokens, &nt);

        int opcode = 0, dest = 0, src1 = 0, src2 = 0;
        int is_branch = 0;

        // Branch
        if (tokens[0][0] == 'B' && tokens[0][1] != '\0') {
            int code = branch_code(tokens[0] + 1);
            if (code >= 0 && nt >= 2) {
                opcode = 0x10 + code;
                dest = 0;
                src1 = 0;
                char *lab = tokens[1];
                if (lab[0] == '.') lab++;
                int target = -1;
                for (int i = 0; i < label_count; i++) {
                    if (strcmp(labels[i], lab) == 0) {
                        target = label_addr[i];
                        break;
                    }
                }
                if (target == -1) {
                    fprintf(stderr, "Undefined label: %s\n", tokens[1]);
                    exit(EXIT_FAILURE);
                }
                int offset = target - instr_index;
                if (offset < -128 || offset > 127) {
                    fprintf(stderr, "Branch offset too large (must be -128 to 127): %d\n", offset);
                    exit(EXIT_FAILURE);
                }
                src2 = offset & 0xFF;   // two's complement byte
                is_branch = 1;
            }
        }
        // Legacy Read: Read <variable> <constant address>
        else if (strcmp(tokens[0], "Read") == 0) {
            if (nt != 3) {
                fprintf(stderr, "Read requires variable and constant address\n");
                exit(EXIT_FAILURE);
            }
            int is_vec, num;
            if (parse_reg(tokens[1], &is_vec, &num) && !is_vec) {
                int addr = check_constant_range(tokens[2]);
                opcode = 0x0D;  // integer memory read const
                dest = num;
                src1 = 0;
                src2 = addr;
            } else {
                fprintf(stderr, "Read requires integer register destination\n");
                exit(EXIT_FAILURE);
            }
        }
        // Legacy Write: Write <variable> <constant address>
        else if (strcmp(tokens[0], "Write") == 0) {
            if (nt != 3) {
                fprintf(stderr, "Write requires variable and constant address\n");
                exit(EXIT_FAILURE);
            }
            int is_vec, num;
            if (parse_reg(tokens[1], &is_vec, &num) && !is_vec) {
                int addr = check_constant_range(tokens[2]);
                opcode = 0x0E;  // integer memory write const
                dest = num;
                src1 = 0;
                src2 = addr;
            } else {
                fprintf(stderr, "Write requires integer register source\n");
                exit(EXIT_FAILURE);
            }
        }
        // Print
        else if (strcmp(tokens[0], "print") == 0) {
            int is_vec, num;
            if (parse_reg(tokens[1], &is_vec, &num) && !is_vec) {
                opcode = 0x08;
                dest = 0;
                src1 = 0;
                src2 = num;
            } else {
                fprintf(stderr, "Print requires integer register\n");
                exit(EXIT_FAILURE);
            }
        }
        // Memory write: [addr] = src
        else if (nt >= 5 && strcmp(tokens[0], "[") == 0 &&
                 strcmp(tokens[2], "]") == 0 && strcmp(tokens[3], "=") == 0) {
            char *addr = tokens[1];
            char *src = tokens[4];
            int a_vec, a_num, s_vec, s_num;
            parse_reg(addr, &a_vec, &a_num);
            parse_reg(src, &s_vec, &s_num);

            if (is_number(addr)) {
                int addr_val = check_constant_range(addr);
                if (s_vec) {
                    opcode = 0x2E;      // vector memory write const
                    dest = s_num;
                    src1 = 0;
                    src2 = addr_val;
                } else {
                    opcode = 0x0E;      // integer memory write const
                    dest = s_num;
                    src1 = 0;
                    src2 = addr_val;
                }
            } else {
                // variable address
                if (s_vec) {
                    opcode = 0x26;      // vector memory write variable
                    dest = a_num;
                    src1 = 0;
                    src2 = s_num;
                } else {
                    opcode = 0x06;      // integer memory write variable
                    dest = a_num;
                    src1 = 0;
                    src2 = s_num;
                }
            }
        }
        // Memory read: dest = [addr]
        else if (nt >= 5 && strcmp(tokens[1], "=") == 0 &&
                 strcmp(tokens[2], "[") == 0 && strcmp(tokens[4], "]") == 0) {
            char *dst = tokens[0];
            char *addr = tokens[3];
            int d_vec, d_num, a_vec, a_num;
            parse_reg(dst, &d_vec, &d_num);
            parse_reg(addr, &a_vec, &a_num);

            if (is_number(addr)) {
                int addr_val = check_constant_range(addr);
                if (d_vec) {
                    opcode = 0x2C;      // vector memory read const
                    dest = d_num;
                    src1 = 0;
                    src2 = addr_val;
                } else {
                    opcode = 0x0D;      // integer memory read const
                    dest = d_num;
                    src1 = 0;
                    src2 = addr_val;
                }
            } else {
                if (d_vec) {
                    opcode = 0x25;      // vector memory read variable
                    dest = d_num;
                    src1 = 0;
                    src2 = a_num;
                } else {
                    opcode = 0x05;      // integer memory read variable
                    dest = d_num;
                    src1 = 0;
                    src2 = a_num;
                }
            }
        }
        // Simple assignment: dest = constant or dest = register (3 tokens)
        else if (nt == 3 && strcmp(tokens[1], "=") == 0) {
            char *dst = tokens[0];
            char *src = tokens[2];
            int d_vec, d_num, s_vec, s_num;
            if (parse_reg(dst, &d_vec, &d_num) && !d_vec) {
                if (is_number(src)) {
                    int val = check_constant_range(src);
                    opcode = 0x0F;   // data movement constant
                    dest = d_num;
                    src1 = 0;
                    src2 = val;
                } else if (parse_reg(src, &s_vec, &s_num) && !s_vec) {
                    opcode = 0x07;   // data movement register
                    dest = d_num;
                    src1 = 0;
                    src2 = s_num;
                }
            }
        }
        // Arithmetic or data movement (>=5 tokens)
        else if (nt >= 5 && strcmp(tokens[1], "=") == 0) {
            char *dst = tokens[0];
            char *op1 = tokens[2];
            char *op = tokens[3];
            char *op2 = tokens[4];

            int d_vec, d_num, o1_vec, o1_num, o2_vec, o2_num;
            parse_reg(dst, &d_vec, &d_num);
            parse_reg(op1, &o1_vec, &o1_num);

            // Removed the early data-movement checks that caused miscompilation.
            // Now only the actual operator is considered.

            if (strcmp(op, "+") == 0) {
                if (d_vec) {
                    if (is_number(op2)) {
                        int val = check_constant_range(op2);
                        opcode = 0x29;
                        dest = d_num;
                        src1 = o1_num;
                        src2 = val;
                    } else if (parse_reg(op2, &o2_vec, &o2_num)) {
                        if (o2_vec) {
                            opcode = 0x21;
                            dest = d_num;
                            src1 = o1_num;
                            src2 = o2_num;
                        } else {
                            opcode = 0x31;
                            dest = d_num;
                            src1 = o1_num;
                            src2 = o2_num;
                        }
                    }
                } else {
                    if (is_number(op2)) {
                        int val = check_constant_range(op2);
                        opcode = 0x09;
                        dest = d_num;
                        src1 = o1_num;
                        src2 = val;
                    } else if (parse_reg(op2, &o2_vec, &o2_num) && !o2_vec) {
                        opcode = 0x01;
                        dest = d_num;
                        src1 = o1_num;
                        src2 = o2_num;
                    }
                }
            }
            else if (strcmp(op, "-") == 0) {
                if (d_vec) {
                    if (is_number(op2)) {
                        int val = check_constant_range(op2);
                        opcode = 0x2A;
                        dest = d_num;
                        src1 = o1_num;
                        src2 = val;
                    } else if (parse_reg(op2, &o2_vec, &o2_num)) {
                        if (o2_vec) {
                            opcode = 0x22;
                            dest = d_num;
                            src1 = o1_num;
                            src2 = o2_num;
                        } else {
                            opcode = 0x32;
                            dest = d_num;
                            src1 = o1_num;
                            src2 = o2_num;
                        }
                    }
                } else {
                    if (is_number(op2)) {
                        int val = check_constant_range(op2);
                        opcode = 0x0A;
                        dest = d_num;
                        src1 = o1_num;
                        src2 = val;
                    } else if (parse_reg(op2, &o2_vec, &o2_num) && !o2_vec) {
                        opcode = 0x02;
                        dest = d_num;
                        src1 = o1_num;
                        src2 = o2_num;
                    }
                }
            }
            else if (strcmp(op, "*") == 0) {
                if (d_vec) {
                    if (is_number(op2)) {
                        int val = check_constant_range(op2);
                        opcode = 0x2B;
                        dest = d_num;
                        src1 = o1_num;
                        src2 = val;
                    } else if (parse_reg(op2, &o2_vec, &o2_num)) {
                        if (o2_vec) {
                            opcode = 0x23;
                            dest = d_num;
                            src1 = o1_num;
                            src2 = o2_num;
                        } else {
                            opcode = 0x33;
                            dest = d_num;
                            src1 = o1_num;
                            src2 = o2_num;
                        }
                    }
                } else {
                    if (is_number(op2)) {
                        int val = check_constant_range(op2);
                        opcode = 0x0B;
                        dest = d_num;
                        src1 = o1_num;
                        src2 = val;
                    } else if (parse_reg(op2, &o2_vec, &o2_num) && !o2_vec) {
                        opcode = 0x03;
                        dest = d_num;
                        src1 = o1_num;
                        src2 = o2_num;
                    }
                }
            }
            else if (strcmp(op, "/") == 0) {
                if (is_number(op2)) {
                    int val = check_constant_range(op2);
                    opcode = 0x0C;
                    dest = d_num;
                    src1 = o1_num;
                    src2 = val;
                } else if (parse_reg(op2, &o2_vec, &o2_num) && !o2_vec) {
                    opcode = 0x04;
                    dest = d_num;
                    src1 = o1_num;
                    src2 = o2_num;
                }
            }
        }

        if (opcode == 0 && !is_branch) {
            fprintf(stderr, "Unrecognised instruction: %s\n", line);
            exit(EXIT_FAILURE);
        }

        fprintf(out, "%X %X %X %X\n", opcode, dest, src1, src2);
        instr_index++;
    }

    // End-of-program marker
    fprintf(out, "0 0 0 0\n");

    fclose(out);
    fclose(src);
}