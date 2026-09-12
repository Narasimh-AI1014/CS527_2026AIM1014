#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

#define MEMSIZE 8192
#define PAGESIZE 512
#define NUM_PHYSICAL_PAGES (MEMSIZE / PAGESIZE)             /* 16 */
#define NUM_LOGICAL_PAGES (1024/PAGESIZE + 4096/PAGESIZE)   /* 2 + 8 = 10 */

extern char memory[MEMSIZE];

char    read_byte(int phys_addr);
void    write_byte(int phys_addr, char value);
int32_t read_word(int phys_addr);
void    write_word(int phys_addr, int32_t value);

#endif /* MEMORY_H */