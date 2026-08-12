#include <stdio.h>
#include "memory.h"


unsigned char Instruction[256];
int Data[256];

void initialize(){
    FILE *fp;
    int i =0 ;
    
    unsigned int op,d,s1,s2;
    
    fp = fopen("program.byte","r");
    
    
    if(fp==NULL){
        printf("program.byte not found.\n");
        return;
    }
    
    while(fscanf(fp,"%u %u %u %u",&op,&d,&s1,&s2)==4){
        Instruction[i++] = op;
        Instruction[i++] = d;
        Instruction[i++] = s1;
        Instruction[i++] = s2;
    }
    fclose(fp);
    
    
    fp = fopen("data.byte","r");
    
    
    if(fp!=NULL){
        for(i=0;i<256;i++){
            fscanf(fp,"%d",&Data[i]); 
        }
        fclose(fp);
    }
    
}


void finalize(){
    FILE *fp;
    fp = fopen("data.byte","w");
    if(fp == NULL){
        return ;
    }
    
    for(int i=0;i<256;i++){
        fprintf(fp,"%d\n",Data[i]);
    }
    fclose(fp);
}