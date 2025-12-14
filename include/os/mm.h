/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                                   Memory Management
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */
#ifndef MM_H
#define MM_H

#include <type.h>
#include <os/smp.h>
#include <pgtable.h>
#include <os/kernel.h>
#include <os/list.h>
#include <os/lock.h>

#define MAP_KERNEL 1
#define MAP_USER 2
#define MEM_SIZE 32
#define PAGE_SIZE 4096 // 4K
#define INIT_KERNEL_STACK 0xffffffc052000000
#define FREEMEM_KERNEL (INIT_KERNEL_STACK+PAGE_SIZE*NR_CPUS)
extern ptr_t kernMemCurr;

/* Rounding; only works for n = power of two */
#define ROUND(a, n)     (((((uint64_t)(a))+(n)-1)) & ~((n)-1))
#define ROUNDDOWN(a, n) (((uint64_t)(a)) & ~((n)-1))

extern ptr_t allocPage(int numPage);
// TODO [P4-task1] */
void freePage(ptr_t baseAddr);

// #define S_CORE
// NOTE: only need for S-core to alloc 2MB large page
#ifdef S_CORE
#define LARGE_PAGE_FREEMEM 0xffffffc056000000
#define USER_STACK_ADDR 0x400000
extern ptr_t allocLargePage(int numPage);
#else
// NOTE: A/C-core
#define USER_STACK_ADDR 0xf00010000
#endif

// TODO [P4-task1] */
extern void* kmalloc(size_t size);
extern void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir);
extern uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir);
extern ptr_t get_user_page();

void recycle_page_table(uintptr_t pgdir);


// [p4-task3]
#define PAGE_SECTORS PAGE_SIZE/512
#define SWAP_START_SEC 500
#define SWAP_MAX_PAGES 100000 // max swap num supported
void swap_in(uintptr_t stval, PTE *pte);
ptr_t swap_out();
void list_add_page(uintptr_t pa, PTE *pte);
void free_swap_slot(int idx);
int alloc_swap_slot();

size_t get_free_memory();

// TODO [P4-task4]: shm_page_get/dt */
uintptr_t shm_page_get(int key);
void shm_page_dt(uintptr_t addr);



// TODO [P4-task5]: zero copy pipe
#define MAX_PIPES 16    // pipe num
#define PIPE_SIZE 1024
#define PIPE_LOCK_KEY_BASE 2000
typedef struct {
    uintptr_t load_addr; // pa: if is_swap=0; disk pos: if is_swap=1
    int is_swap;       
} pipe_page_t;

typedef struct {
    int valid;
    char name[32];
    pipe_page_t pages[PIPE_SIZE];
    int head; 
    int tail; 
    int count;
    int mutex;
    list_head wait_queue;
} pipe_t; 

extern void init_pipes();
extern PTE* get_pte_ptr(uintptr_t pgdir, uintptr_t va);
extern pipe_t pipes[MAX_PIPES];
int do_pipe_open(const char *name);
long do_pipe_give_pages(int pipe_idx, void *src, size_t length);
long do_pipe_take_pages(int pipe_idx, void *dst, size_t length);

#endif /* MM_H */
