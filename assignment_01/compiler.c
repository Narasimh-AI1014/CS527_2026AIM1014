#include <stdio.h>
#include <string.h>

#include "compiler.h"

void compile() {
	FILE *in;
	FILE *out;

	char line[100];

	in = fopen("math_operation.txt","r");

	if(in == NULL) {
		printf("math_oeration.txt not found.\n");
		return;
	}

	out = fopen("program.byte","w");

	if(out == NULL) {
		printf("cannot create program.byte file.\n");
		return;
	}

	while(fgets(line,sizeof(line),in)!=NULL) {
		int d,s1,s2;


		//Read command
		if(sscanf(line,"Read x%d, %d",&d,&s1)==2) {
			fprintf(out,"5 %d %d 0\n",d,s1);
		}


		//Write command
		else if(sscanf(line,"Write x%d, %d",&d,&s1)==2) {
			fprintf(out,"6 %d %d 0\n",d,s1);
		}

		// Addition
		else if(strchr(line,'+')) {
			sscanf(line,"x%d = x%d + x%d",&d,&s1,&s2);
			fprintf(out,"1 %d %d %d\n",d,s1,s2);
		}

		// Substraction
		else if(strchr(line,'-')) {
			sscanf(line,"x%d = x%d - x%d",&d,&s1,&s2);
			fprintf(out,"2 %d %d %d\n",d,s1,s2);
		}

		//Multiply
		else if(strchr(line,'*')) {
			sscanf(line,"x%d = x%d * x%d",&d,&s1,&s2);
			fprintf(out,"4 %d %d %d\n",d,s1,s2);
		}


		//Move constant
		else if(sscanf(line,"x%d = %d",&d,&s1) == 2) {
			fprintf(out,"7 %d %d 0\n",d,s1);
		}
	}

	// Halt instruction
	fprintf(out,"0 0 0 0\n");
	fclose(in);
	fclose(out);

	printf("comilation complete");
}