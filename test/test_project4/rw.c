#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

int main(int argc, char* argv[])
{
	assert(argc > 1);
	srand(clock());
	long mem2 = 0;
	uintptr_t mem1 = 0;
	int curs = 0;
	int i;
	sys_move_cursor(0, 2);
	for (i = 2; i < argc; i++)
	{
		if(strcmp(argv[i], "&") == 0)	continue;
		else if(strncmp(argv[i], "0x", 2) != 0)	
		{
			printf("Input address type error: expect 0x+num\n");
			continue;
		}
		mem1 = strtoull_simple(argv[i], 16);
		// sys_move_cursor(2, curs+i);
		mem2 = rand();
		*(long*)mem1 = mem2;
		printf("0x%lx, %ld\n", mem1, mem2);
		if (*(long*)mem1 != mem2) {
			printf("Error!\n");
		}
	}
	//Only input address.
	//Achieving input r/w command is recommended but not required.
	printf("Success!\n");
	return 0;
}
