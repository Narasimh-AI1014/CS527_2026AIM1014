#include "compiler.h"
#include "memory.h"
#include "processor.h"

#include <stdio.h>
int main()
{

    compile();


    initialize();


    reset();



    while(end_of_simulation==1)
    {

        fetch();

        decode();

        execute();

    }



    finalize();



    printf("Simulation completed\n");


    return 0;

}