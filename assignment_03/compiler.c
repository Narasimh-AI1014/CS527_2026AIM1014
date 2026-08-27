#include "compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE_LEN 256
#define MAX_TOKENS 32

// Token categories for the lexical analyzer
typedef enum {
    TOKEN_REGISTER_INT, // x0 - x255
    TOKEN_REGISTER_VEC, // v0 - v31
    TOKEN_CONSTANT,     // 0 - 255
    TOKEN_OP,           // +, -, *, /
    TOKEN_ASSIGN,       // =
    TOKEN_LBRACKET,     // [
    TOKEN_RBRACKET,     // ]
    TOKEN_LABEL,        // .name
    TOKEN_BRANCH,       // B<suffix>
    TOKEN_END
} TokenType;

typedef struct {
    TokenType type;
    int value;          // Register index or constant numerical value
    char text[64];      // Raw string representation (for labels/branches)
} Token;

static Label label_table[MAX_LABELS];
static int label_count = 0;

static const char *suffixes[] = {
    "EQ", "NE", "CS", "CC", "MI", "PL", "VS", "VC",
    "HI", "LS", "GE", "LT", "GT", "LE", "AL"
};

// --- LEXER: Breaks raw character stream into valid language tokens ---
static int tokenize(const char *input, Token *tokens) {
    int t_count = 0;
    int i = 0;
    int len = strlen(input);

    while (i < len && t_count < MAX_TOKENS - 1) {
        // Skip whitespaces
        if (isspace((unsigned char)input[i])) {
            i++;
            continue;
        }
        // Skip comments entirely
        if (input[i] == '%') {
            break;
        }

        // Structural brackets and assignment operators
        if (input[i] == '=') {
            tokens[t_count++].type = TOKEN_ASSIGN;
            i++;
            continue;
        }
        if (input[i] == '[') {
            tokens[t_count++].type = TOKEN_LBRACKET;
            i++;
            continue;
        }
        if (input[i] == ']') {
            tokens[t_count++].type = TOKEN_RBRACKET;
            i++;
            continue;
        }
        if (input[i] == '+' || input[i] == '-' || input[i] == '*' || input[i] == '/') {
            tokens[t_count].type = TOKEN_OP;
            tokens[t_count++].value = input[i];
            i++;
            continue;
        }

        // Parse labels (e.g., .loopback)
        if (input[i] == '.') {
            int start = i;
            while (i < len && !isspace((unsigned char)input[i]) && input[i] != '%') i++;
            int token_len = i - start;
            if (token_len > 63) token_len = 63;
            tokens[t_count].type = TOKEN_LABEL;
            snprintf(tokens[t_count].text, sizeof(tokens[t_count].text), "%.*s", token_len, &input[start]);
            t_count++;
            continue;
        }

        // Parse words (Registers and Branch Suffixes)
        if (isalpha((unsigned char)input[i])) {
            int start = i;
            while (i < len && (isalnum((unsigned char)input[i]))) i++;
            int token_len = i - start;
            char word[64];
            snprintf(word, sizeof(word), "%.*s", token_len, &input[start]);

            // Branch Instructions
            if (toupper((unsigned char)word[0]) == 'B') {
                tokens[t_count].type = TOKEN_BRANCH;
                snprintf(tokens[t_count].text, sizeof(tokens[t_count].text), "%s", word);
                t_count++;
            }
            // Integer Registers (x0 - x255)
            else if (toupper((unsigned char)word[0]) == 'X') {
                tokens[t_count].type = TOKEN_REGISTER_INT;
                tokens[t_count++].value = atoi(word + 1);
            }
            // Vector Registers (v0 - v31)
            else if (toupper((unsigned char)word[0]) == 'V') {
                tokens[t_count].type = TOKEN_REGISTER_VEC;
                tokens[t_count++].value = atoi(word + 1);
            }
            continue;
        }

        // Parse Numerical Constants
        if (isdigit((unsigned char)input[i])) {
            int val = 0;
            while (i < len && isdigit((unsigned char)input[i])) {
                val = val * 10 + (input[i] - '0');
                i++;
            }
            tokens[t_count].type = TOKEN_CONSTANT;
            tokens[t_count++].value = val;
            continue;
        }

        i++; // Fallback loop increment for unknown input symbols
    }

    tokens[t_count].type = TOKEN_END;
    return t_count;
}

void compile(const char *source_filename) {
    FILE *src = fopen(source_filename, "r");
    if (!src) {
        perror("Compiler Error: Cannot open source assembly file");
        exit(EXIT_FAILURE);
    }

    char line[MAX_LINE_LEN];
    Token tokens[MAX_TOKENS];
    int current_address = 0;
    label_count = 0;

    // --- PASS 1: Structural Scan for Label Mapping ---
    while (fgets(line, sizeof(line), src)) {
        int count = tokenize(line, tokens);
        if (count == 0 || tokens[0].type == TOKEN_END) continue;

        if (tokens[0].type == TOKEN_LABEL) {
            if (label_count < MAX_LABELS) {
                snprintf(label_table[label_count].name, sizeof(label_table[label_count].name), "%s", tokens[0].text);
                label_table[label_count].address = current_address;
                label_count++;
            }
        } else {
            current_address++;
        }
    }

    // --- PASS 2: Parser Rules Implementation & Code Generation ---
    fseek(src, 0, SEEK_SET);
    FILE *out = fopen("program.byte", "w");
    if (!out) {
        perror("Compiler Error: Cannot generate program.byte target");
        fclose(src);
        exit(EXIT_FAILURE);
    }

    current_address = 0;
    while (fgets(line, sizeof(line), src)) {
        int count = tokenize(line, tokens);
        if (count == 0 || tokens[0].type == TOKEN_END || tokens[0].type == TOKEN_LABEL) continue;

        unsigned char opcode = 0, dest = 0, op1 = 0, op2 = 0;

        // Rule 1: Conditional Branch Parsing (e.g., BEQ .exit)
        if (tokens[0].type == TOKEN_BRANCH && tokens[1].type == TOKEN_LABEL) {
            char suffix[16] = {0};
            snprintf(suffix, sizeof(suffix), "%s", tokens[0].text + 1);
            for (int i = 0; suffix[i]; i++) suffix[i] = toupper((unsigned char)suffix[i]);

            int cond_code = 14; // Default index value for AL (Always)
            for (int i = 0; i < 15; i++) {
                if (strcmp(suffix, suffixes[i]) == 0) {
                    cond_code = i;
                    break;
                }
            }
            opcode = 0x10 + cond_code;

            int target_address = -1;
            for (int i = 0; i < label_count; i++) {
                if (strcmp(label_table[i].name, tokens[1].text) == 0) {
                    target_address = label_table[i].address;
                    break;
                }
            }
            char offset = (char)(target_address - current_address);
            op2 = (unsigned char)offset;

            fprintf(out, "%X %X %X %X\n", opcode, dest, op1, op2);
            current_address++;
            continue;
        }

        // Rule 2: Memory Writes / Store Parsing (e.g., [x3] = x2 or [x2] = v1)
        if (tokens[0].type == TOKEN_LBRACKET) {
            int is_dest_var = (tokens[1].type == TOKEN_REGISTER_INT);
            dest = tokens[1].value; // holds either reg number or absolute constant address

            int is_src_vec = (tokens[4].type == TOKEN_REGISTER_VEC);
            op2 = tokens[4].value;
            op1 = 0; // Specification rule: operand 1 is 0 for memory operations

            if (is_src_vec) {
                opcode = is_dest_var ? 0x26 : 0x2E; // Vector Store (Var vs Const)
            } else {
                opcode = is_dest_var ? 0x06 : 0x0E; // Integer Store (Var vs Const)
            }

            fprintf(out, "%X %X %X %X\n", opcode, dest, op1, op2);
            current_address++;
            continue;
        }

        // Rule 3: Destination Assignments & Math Operations
        int is_dest_vec = (tokens[0].type == TOKEN_REGISTER_VEC);
        dest = tokens[0].value;

        // Sub-rule A: Load or Data Movement (Tokens count == 3 or brackets layout)
        if (tokens[1].type == TOKEN_ASSIGN && tokens[2].type != TOKEN_REGISTER_INT && 
            tokens[2].type != TOKEN_REGISTER_VEC && tokens[2].type != TOKEN_LBRACKET) {
            // Data movement constant mapping (x1 = 10)
            opcode = is_dest_vec ? 0x2F : 0x0F;
            op1 = 0;
            op2 = tokens[2].value;
        }
        else if (tokens[2].type == TOKEN_LBRACKET) {
            // Bracket Memory Reads (v1 = [x2] or x1 =)
            int is_src_var = (tokens[3].type == TOKEN_REGISTER_INT);
            op2 = tokens[3].value;
            op1 = 0;

            if (is_dest_vec) {
                opcode = is_src_var ? 0x25 : 0x2C; // Vector Load (Var vs Const)
            } else {
                opcode = is_src_var ? 0x05 : 0x0D; // Integer Load (Var vs Const)
            }
        }
        // Sub-rule B: Arithmetic Expressions Parsing (e.g., v3 = v1 + v4)
        else if (tokens[3].type == TOKEN_OP) {
            int is_op1_vec = (tokens[2].type == TOKEN_REGISTER_VEC);
            op1 = tokens[2].value;

            int is_op2_const = (tokens[4].type == TOKEN_CONSTANT);
            op2 = tokens[4].value;
            int op_char = tokens[3].value;

            if (is_dest_vec || is_op1_vec) {
                // Vector Operations
                if (op_char == '+') opcode = is_op2_const ? 0x29 : 0x21;
                else if (op_char == '-') opcode = is_op2_const ? 0x2A : 0x22;
                else if (op_char == '*') opcode = is_op2_const ? 0x2B : 0x23;
            } else {
                // Integer Operations
                if (op_char == '+') opcode = is_op2_const ? 0x09 : 0x01;
                else if (op_char == '-') opcode = is_op2_const ? 0x0A : 0x02;
                else if (op_char == '*') opcode = is_op2_const ? 0x0B : 0x03;
                else if (op_char == '/') opcode = is_op2_const ? 0x0C : 0x04;
            }
        }

fprintf(out, "%X %X %X %X\n", opcode, dest, op1, op2);current_address++;}fclose(src);fclose(out);
}