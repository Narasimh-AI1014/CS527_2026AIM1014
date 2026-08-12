#include <stdio.h>
#include "processor.h"
#include "memory.h"

int Register[256];

int PC;
int opcode;
int dest;
int src1;
int src2;

int end_of_simulation = 0;


void reset(){
    for(int i=0;i<256;i++){
        Register[i] = 0;
    }
    PC = 0;
}


void fetch(){
    
    opcode = Instruction[PC++];
    dest = Instruction[PC++];
    src1 = Instruction[PC++];
    src2 = Instruction[PC++];
}

void decode(){
    
}

void execute(){
    switch(opcode){
        
        case 0:
            end_of_simulation = 1;
            break;
        case 1:
            Register[dest] = Register[src1] + Register[src2];
            break;
        case 2:
            Register[dest] = Register[src1] - Register[src2];
            break;
        case 3:
            Register[dest] = Register[src1] * Register[src2];
            break;
        case 4:
            Register[dest] = Register[src1] / Register[src2];
            break;
        case 5:
            Register[dest] = Data[src1];
            break;
        case 6:
            Data[src1] = Register[dest];
            break;
        case 7:
            Register[dest] = src1;
            break;
        default:
            printf("Invalid opcode. \n");
            end_of_simulation=1;
            
    }
}