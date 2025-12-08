#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>

// MAX_USER_PAGES = 256
#define TEST_PAGE_NUM 350 
#define PAGE_SIZE 4096

#define MEM_BASE_ADDR 0x10000000 

int main(int argc, char *argv[])
{
    sys_move_cursor(0, 0);
    printf("=== Start Swap Test ===\n");

    volatile long *mem_ptr = (long *)MEM_BASE_ADDR;
    
    for (int i = 0; i < TEST_PAGE_NUM; i++) 
    {
        long *curr_addr = (long *)(MEM_BASE_ADDR + i * PAGE_SIZE);
        *curr_addr = i; 
    }
    printf("1. Write Done.\n");
    
    printf("2. Verifying data ...\n");
    int error_cnt = 0;
    for (int i = 0; i < TEST_PAGE_NUM; i++) 
    {
        long *curr_addr = (long *)(MEM_BASE_ADDR + i * PAGE_SIZE);
        long val = *curr_addr;
        long expected = i;

        if (val != expected) 
        {
            printf("ERROR at page %d! Addr: %lx, Val: %lx, Expect: %lx\n", 
                   i, (long)curr_addr, val, expected);
            error_cnt++;
        }
    }

    if (error_cnt == 0) {
        printf("=== SUCCESS: All data matches! ===\n");
    } else {
        printf("=== FAILED: Found %d errors ===\n", error_cnt);
    }
}