#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <stdarg.h>
#include "compiler.h"

#define MAX_TOKENS 16
#define MAX_LABELS 256
#define TOKEN_LEN  32

/* ---------- Error handling ---------- */

static jmp_buf g_err_jmp;
static FILE *g_src = NULL;
static FILE *g_out = NULL;

static void compile_fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    longjmp(g_err_jmp, 1);
}

/* ---------- Utility functions ---------- */

static char *trim(char *s) {
    char *start = s;
    while (isspace((unsigned char)*start)) start++;
    if (*start == '\0') { *s = '\0'; return s; }
    memmove(s, start, strlen(start) + 1);
    char *end = s + strlen(s) - 1;
    while (end >= s && isspace((unsigned char)*end)) { *end = '\0'; end--; }
    return s;
}

static void strip_comment(char *s) {
    char *p = strchr(s, '%');
    if (p) *p = '\0';
}

static void tokenize(char *line, char tokens[][TOKEN_LEN], int *n) {
    char temp[3 * 256 + 1];
    size_t j = 0;
    const size_t cap = sizeof(temp);

    for (size_t i = 0; line[i]; i++) {
        if (j + 4 > cap) {
            fprintf(stderr, "warning: line too long, truncated: %.60s...\n", line);
            break;
        }
        if (line[i] == '[') {
            temp[j++] = ' '; temp[j++] = '['; temp[j++] = ' ';
        } else if (line[i] == ']') {
            temp[j++] = ' '; temp[j++] = ']'; temp[j++] = ' ';
        } else {
            temp[j++] = line[i];
        }
    }
    temp[j] = '\0';

    *n = 0;
    char *tok = strtok(temp, " \t");
    while (tok && *n < MAX_TOKENS) {
        strncpy(tokens[*n], tok, TOKEN_LEN - 1);
        tokens[*n][TOKEN_LEN - 1] = '\0';
        (*n)++;
        tok = strtok(NULL, " \t");
    }
}

/* ---------- Register and number parsing ---------- */

static int parse_reg(const char *s, int *is_vec, int *num) {
    if (s[0] == 'x' || s[0] == 'X') {
        if (s[1] == '\0') return 0;
        for (const char *p = s + 1; *p; p++)
            if (!isdigit((unsigned char)*p)) return 0;
        *is_vec = 0;
        *num = atoi(s + 1);
        if (*num < 0 || *num >= NUM_INT_REG)
            compile_fail("Invalid integer register: %s\n", s);
        return 1;
    }
    if (s[0] == 'v' || s[0] == 'V') {
        if (s[1] == '\0') return 0;
        for (const char *p = s + 1; *p; p++)
            if (!isdigit((unsigned char)*p)) return 0;
        *is_vec = 1;
        *num = atoi(s + 1);
        if (*num < 0 || *num >= NUM_VEC_REG)
            compile_fail("Invalid vector register: %s\n", s);
        return 1;
    }
    return 0;
}

/* Parse an integer register; reject x0 as a write target. */
static int parse_int_reg_dest(const char *s, int *num) {
    int is_vec, n;
    if (!parse_reg(s, &is_vec, &n) || is_vec) return 0;
    if (n == 0) compile_fail("Cannot write to x0 (hardwired zero): %s\n", s);
    *num = n;
    return 1;
}

static int is_number(const char *s) {
    if (*s == '\0') return 0;
    for (const char *p = s; *p; p++)
        if (!isdigit((unsigned char)*p)) return 0;
    return 1;
}

static int check_constant_range(const char *s) {
    char *endp = NULL;
    long val = strtol(s, &endp, 10);
    if (*endp != '\0' || val < 0 || val > 255)
        compile_fail("Constant out of range (0-255): %s\n", s);
    return (int)val;
}

/* ---------- Branch condition codes ---------- */

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

/* ---------- Label helpers ---------- */

static int add_label(char labels[][64], int label_addr[], int *label_count,
                     const char *name, int instr_index) {
    if (*label_count >= MAX_LABELS)
        compile_fail("Too many labels (max %d)\n", MAX_LABELS);
    strncpy(labels[*label_count], name, 63);
    labels[*label_count][63] = '\0';
    label_addr[*label_count] = instr_index;
    (*label_count)++;
    return 0;
}

/* Strip all leading labels (including multiples on one line) from `line`.
   Returns the number of instructions the labels precede (0 or 1, but kept
   general for future multi-instruction lines). */
static int consume_labels(char *line, char labels[][64], int label_addr[],
                          int *label_count, int instr_index) {
    int consumed_instr = 0;
    while (line[0] == '.') {
        char *p = line + 1;
        char *end = p;
        while (*end && !isspace((unsigned char)*end)) end++;
        char saved = *end;
        *end = '\0';

        add_label(labels, label_addr, label_count, p, instr_index);

        *end = saved;
        char *rest = end;
        while (isspace((unsigned char)*rest)) rest++;
        memmove(line, rest, strlen(rest) + 1);
        trim(line);
    }
    if (strlen(line) > 0) consumed_instr = 1;
    return consumed_instr;
}

/* ---------- Main compilation function ---------- */

int compile(const char *source_filename, const char *prog_filename) {
    g_src = NULL;
    g_out = NULL;

    if (setjmp(g_err_jmp)) {
        if (g_src) { fclose(g_src); g_src = NULL; }
        if (g_out) { fclose(g_out); g_out = NULL; }
        return -1;
    }

    g_src = fopen(source_filename, "r");
    if (!g_src) { perror("fopen source"); return -1; }

    char line[256];
    char labels[MAX_LABELS][64];
    int label_addr[MAX_LABELS];
    int label_count = 0;

    /* ---- First pass: collect labels ---- */
    int instr_index = 0;
    while (fgets(line, sizeof(line), g_src)) {
        strip_comment(line);
        trim(line);
        instr_index += consume_labels(line, labels, label_addr, &label_count, instr_index);
    }

    rewind(g_src);

    g_out = fopen(prog_filename, "w");
    if (!g_out) {
        perror("fopen prog");
        fclose(g_src); g_src = NULL;
        return -1;
    }

    /* ---- Second pass: emit instructions ---- */
    instr_index = 0;
    while (fgets(line, sizeof(line), g_src)) {
        strip_comment(line);
        trim(line);

        /* Strip labels (again, may be multiple). */
        while (line[0] == '.') {
            char *p = line + 1;
            while (*p && !isspace((unsigned char)*p)) p++;
            while (isspace((unsigned char)*p)) p++;
            memmove(line, p, strlen(p) + 1);
            trim(line);
        }
        if (strlen(line) == 0)
            continue;

        char tokens[MAX_TOKENS][TOKEN_LEN];
        int nt;
        tokenize(line, tokens, &nt);
        if (nt == 0)
            continue;

        int opcode = 0, dest = 0, src1 = 0, src2 = 0;

        int bcode = -1;
        if (tokens[0][0] == 'B' && tokens[0][1] != '\0')
            bcode = branch_code(tokens[0] + 1);

        if (bcode >= 0) {
            if (nt != 2)
                compile_fail("Branch instruction requires exactly one label operand: %s\n", line);
            opcode = 0x10 + bcode;
            char *lab = tokens[1];
            if (lab[0] == '.') lab++;
            int target = -1;
            for (int i = 0; i < label_count; i++) {
                if (strcmp(labels[i], lab) == 0) { target = label_addr[i]; break; }
            }
            if (target == -1)
                compile_fail("Undefined label: %s\n", tokens[1]);
            int offset = target - instr_index;
            if (offset < -128 || offset > 127)
                compile_fail("Branch offset too large (must be -128 to 127): %d\n", offset);
            src2 = offset & 0xFF;
        }
        else if (strcmp(tokens[0], "Read") == 0) {
            if (nt != 3)
                compile_fail("Read requires variable and constant address: %s\n", line);
            int num;
            if (!parse_int_reg_dest(tokens[1], &num))
                compile_fail("Read requires integer register destination (not x0): %s\n", line);
            opcode = 0x0D;
            dest = num;
            src2 = check_constant_range(tokens[2]);
        }
        else if (strcmp(tokens[0], "Write") == 0) {
            if (nt != 3)
                compile_fail("Write requires variable and constant address: %s\n", line);
            int is_vec, num;
            if (!parse_reg(tokens[1], &is_vec, &num) || is_vec)
                compile_fail("Write requires integer register source: %s\n", line);
            opcode = 0x0E;
            dest = num;
            src2 = check_constant_range(tokens[2]);
        }
        else if (strcmp(tokens[0], "print") == 0) {
            if (nt != 2)
                compile_fail("Print requires exactly one integer register: %s\n", line);
            int is_vec, num;
            if (!parse_reg(tokens[1], &is_vec, &num) || is_vec)
                compile_fail("Print requires integer register: %s\n", line);
            opcode = 0x08;
            src2 = num;
        }
        else if (strcmp(tokens[0], "[") == 0) {
            if (nt != 5 || strcmp(tokens[2], "]") != 0 || strcmp(tokens[3], "=") != 0)
                compile_fail("Memory write syntax should be: [address] = source: %s\n", line);
            char *addr = tokens[1];
            char *srcop = tokens[4];
            int a_vec, a_num, s_vec, s_num;

            if (is_number(addr)) {
                int addr_val = check_constant_range(addr);
                if (!parse_reg(srcop, &s_vec, &s_num))
                    compile_fail("Source must be a register: %s\n", line);
                if (s_vec) { opcode = 0x2E; dest = s_num; src2 = addr_val; }
                else       { opcode = 0x0E; dest = s_num; src2 = addr_val; }
            } else {
                if (!parse_reg(addr, &a_vec, &a_num) || a_vec)
                    compile_fail("Address must be an integer register or constant: %s\n", line);
                if (!parse_reg(srcop, &s_vec, &s_num))
                    compile_fail("Source must be a register: %s\n", line);
                if (s_vec) { opcode = 0x26; dest = a_num; src2 = s_num; }
                else       { opcode = 0x06; dest = a_num; src2 = s_num; }
            }
        }
        else if (nt >= 3 && strcmp(tokens[1], "=") == 0 && strcmp(tokens[2], "[") == 0) {
            if (nt != 5 || strcmp(tokens[4], "]") != 0)
                compile_fail("Memory read syntax should be: destination = [address]: %s\n", line);
            char *dst = tokens[0];
            char *addr = tokens[3];
            int d_vec, d_num, a_vec, a_num;

            if (!parse_reg(dst, &d_vec, &d_num))
                compile_fail("Destination must be a register: %s\n", line);
            if (!d_vec && d_num == 0)
                compile_fail("Cannot write to x0 (hardwired zero): %s\n", line);

            if (is_number(addr)) {
                int addr_val = check_constant_range(addr);
                if (d_vec) { opcode = 0x2C; dest = d_num; src2 = addr_val; }
                else       { opcode = 0x0D; dest = d_num; src2 = addr_val; }
            } else {
                if (!parse_reg(addr, &a_vec, &a_num) || a_vec)
                    compile_fail("Address must be an integer register or constant: %s\n", line);
                if (d_vec) { opcode = 0x25; dest = d_num; src2 = a_num; }
                else       { opcode = 0x05; dest = d_num; src2 = a_num; }
            }
        }
        else if (nt == 3 && strcmp(tokens[1], "=") == 0) {
            char *dst = tokens[0];
            char *srcop = tokens[2];
            int d_num, s_vec, s_num;

            if (!parse_int_reg_dest(dst, &d_num))
                compile_fail("Destination must be an integer register (not x0): %s\n", line);

            if (is_number(srcop)) {
                opcode = 0x0F;
                dest = d_num;
                src2 = check_constant_range(srcop);
            } else if (parse_reg(srcop, &s_vec, &s_num) && !s_vec) {
                opcode = 0x07;
                dest = d_num;
                src2 = s_num;
            } else {
                compile_fail("Invalid source operand: %s\n", srcop);
            }
        }
        else if (nt >= 5 && strcmp(tokens[1], "=") == 0) {
            if (nt != 5)
                compile_fail("Arithmetic instruction must have exactly 5 tokens: %s\n", line);
            char *dst = tokens[0];
            char *op1 = tokens[2];
            char *op  = tokens[3];
            char *op2 = tokens[4];

            int d_vec, d_num, o1_vec, o1_num, o2_vec, o2_num;
            if (!parse_reg(dst, &d_vec, &d_num))
                compile_fail("Invalid destination register: %s\n", dst);
            if (!d_vec && d_num == 0)
                compile_fail("Cannot write to x0 (hardwired zero): %s\n", line);
            if (!parse_reg(op1, &o1_vec, &o1_num))
                compile_fail("Invalid first operand register: %s\n", op1);

            if (strcmp(op, "+") == 0) {
                if (d_vec) {
                    if (is_number(op2)) {
                        opcode = 0x29; dest = d_num; src1 = o1_num;
                        src2 = check_constant_range(op2);
                    } else if (parse_reg(op2, &o2_vec, &o2_num)) {
                        opcode = o2_vec ? 0x21 : 0x31;
                        dest = d_num; src1 = o1_num; src2 = o2_num;
                    } else compile_fail("Invalid second operand: %s\n", op2);
                } else {
                    if (is_number(op2)) {
                        opcode = 0x09; dest = d_num; src1 = o1_num;
                        src2 = check_constant_range(op2);
                    } else if (parse_reg(op2, &o2_vec, &o2_num) && !o2_vec) {
                        opcode = 0x01; dest = d_num; src1 = o1_num; src2 = o2_num;
                    } else compile_fail("Invalid second operand for integer add: %s\n", op2);
                }
            }
            else if (strcmp(op, "-") == 0) {
                if (d_vec) {
                    if (is_number(op2)) {
                        opcode = 0x2A; dest = d_num; src1 = o1_num;
                        src2 = check_constant_range(op2);
                    } else if (parse_reg(op2, &o2_vec, &o2_num)) {
                        opcode = o2_vec ? 0x22 : 0x32;
                        dest = d_num; src1 = o1_num; src2 = o2_num;
                    } else compile_fail("Invalid second operand: %s\n", op2);
                } else {
                    if (is_number(op2)) {
                        opcode = 0x0A; dest = d_num; src1 = o1_num;
                        src2 = check_constant_range(op2);
                    } else if (parse_reg(op2, &o2_vec, &o2_num) && !o2_vec) {
                        opcode = 0x02; dest = d_num; src1 = o1_num; src2 = o2_num;
                    } else compile_fail("Invalid second operand for integer subtract: %s\n", op2);
                }
            }
            else if (strcmp(op, "*") == 0) {
                if (d_vec) {
                    if (is_number(op2)) {
                        opcode = 0x2B; dest = d_num; src1 = o1_num;
                        src2 = check_constant_range(op2);
                    } else if (parse_reg(op2, &o2_vec, &o2_num)) {
                        opcode = o2_vec ? 0x23 : 0x33;
                        dest = d_num; src1 = o1_num; src2 = o2_num;
                    } else compile_fail("Invalid second operand: %s\n", op2);
                } else {
                    if (is_number(op2)) {
                        opcode = 0x0B; dest = d_num; src1 = o1_num;
                        src2 = check_constant_range(op2);
                    } else if (parse_reg(op2, &o2_vec, &o2_num) && !o2_vec) {
                        opcode = 0x03; dest = d_num; src1 = o1_num; src2 = o2_num;
                    } else compile_fail("Invalid second operand for integer multiply: %s\n", op2);
                }
            }
            else if (strcmp(op, "/") == 0) {
                if (d_vec)
                    compile_fail("Vector division is not supported: %s\n", line);
                if (is_number(op2)) {
                    opcode = 0x0C; dest = d_num; src1 = o1_num;
                    src2 = check_constant_range(op2);
                } else if (parse_reg(op2, &o2_vec, &o2_num) && !o2_vec) {
                    opcode = 0x04; dest = d_num; src1 = o1_num; src2 = o2_num;
                } else compile_fail("Invalid second operand for integer divide: %s\n", op2);
            }
            else compile_fail("Unsupported operator: %s\n", op);
        }
        else {
            compile_fail("Unrecognised instruction: %s\n", line);
        }

        fprintf(g_out, "%X %X %X %X\n", opcode, dest, src1, src2);
        instr_index++;
    }

    fprintf(g_out, "0 0 0 0\n");
    fclose(g_out); g_out = NULL;
    fclose(g_src); g_src = NULL;
    return 0;
}