#include <os/mm.h>
#include <os/string.h>
#include <os/sched.h>
#include <printk.h>
#include <screen.h>

// NOTE: A/C-core
ptr_t kernMemCurr = FREEMEM_KERNEL;
static ptr_t freePageList = 0;
#define MEM_START 0xffffffc050000000
#define MEM_END 0xffffffc060000000


// Swap logic
#define MAX_USER_PAGES 1024   
int current_pages = 0;
typedef struct 
{
    uintptr_t pa;
    PTE *pte;    
} swap_entry_t;

static swap_entry_t swap_queue[MAX_USER_PAGES];
static int swap_head = 0;
static int swap_tail = 0;
#define PTE_PFN_MASK (((1ULL << 44) - 1) << _PAGE_PFN_SHIFT)
static int swap_disk_map[SWAP_MAX_PAGES];

size_t free_pages_count = 0; 

ptr_t allocPage(int numPage)
{
    // if distribute 1 page && freelist not empty
    if (numPage == 1 && freePageList != 0) 
    {
        ptr_t ret = freePageList;
        freePageList = *(ptr_t *)ret;   // point to next
        memset((void*)ret, 0, PAGE_SIZE); 
        return ret;
    }

    // align PAGE_SIZE
    ptr_t ret = ROUND(kernMemCurr, PAGE_SIZE);
    kernMemCurr = ret + numPage * PAGE_SIZE;
    memset((void*)ret, 0, numPage * PAGE_SIZE);
    return ret;
}

int is_swap_queue_full()
{
    int next_tail = (swap_tail + 1) % MAX_USER_PAGES;
    return next_tail == swap_head;
}

ptr_t get_user_page()
{
    if (is_swap_queue_full()) 
    {
        ptr_t victim_page = swap_out();
        if (victim_page == 0) 
        {
            printk("Panic: Out of memory and Swap\n");
            return 0; 
        }
        return victim_page;
    }
    // queue not full: alloc directly    
    return allocPage(1);
}

// NOTE: Only need for S-core to alloc 2MB large page
#ifdef S_CORE
static ptr_t largePageMemCurr = LARGE_PAGE_FREEMEM;
ptr_t allocLargePage(int numPage)
{
    // align LARGE_PAGE_SIZE
    ptr_t ret = ROUND(largePageMemCurr, LARGE_PAGE_SIZE);
    largePageMemCurr = ret + numPage * LARGE_PAGE_SIZE;
    return ret;    
}
#endif

void freePage(ptr_t baseAddr)
{
    // TODO [P4-task1] (design you 'freePage' here if you need):
    if (baseAddr == (ptr_t)NULL) return;
    if (baseAddr < MEM_START || baseAddr >= MEM_END) return; 

    // *(ptr_t *)baseAddr = freePageList;
    // freePageList = baseAddr;
    free_pages_count++;
}

void *kmalloc(size_t size)
{
    // TODO [P4-task1] (design you 'kmalloc' here if you need):
    int num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    return (void *)allocPage(num_pages);
}


/* this is used for mapping kernel virtual address into user page table */
void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir)
{
    // TODO [P4-task1] share_pgtable:
    memcpy((void*)dest_pgdir, (void*)src_pgdir, PAGE_SIZE);
}

/* allocate physical page for `va`, mapping it into `pgdir`,
   return the kernel virtual address for the page
   */
uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir)
{
    // TODO [P4-task1] alloc_page_helper:
    // VA: [38:30] VPN2 | [29:21] VPN1 | [20:12] VPN0 | [11:0] Offset
    uint64_t vpn2 = (va >> 30) & 0x1FF;
    uint64_t vpn1 = (va >> 21) & 0x1FF;
    uint64_t vpn0 = (va >> 12) & 0x1FF;

    PTE *pgdir_kva = (PTE *)pgdir;
    // ---------------- Level 2 ----------------
    if ((pgdir_kva[vpn2] & _PAGE_PRESENT) == 0) 
    {
        uintptr_t new_page_kva = allocPage(1);
        set_pfn(&pgdir_kva[vpn2], kva2pa(new_page_kva) >> NORMAL_PAGE_SHIFT);   
        set_attribute(&pgdir_kva[vpn2], _PAGE_PRESENT); // not leaf: only set V
    }
    // set pte: point to next level
    PTE *pmd_kva = (PTE *)pa2kva(get_pa(pgdir_kva[vpn2]));
    // ---------------- Level 1 ----------------
    if ((pmd_kva[vpn1] & _PAGE_PRESENT)== 0) 
    {
        uintptr_t new_page_kva = allocPage(1);
        set_pfn(&pmd_kva[vpn1], kva2pa(new_page_kva) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pmd_kva[vpn1], _PAGE_PRESENT);
    }

    PTE *pte_kva = (PTE *)pa2kva(get_pa(pmd_kva[vpn1]));
    // ---------------- Level 0 (Leaf) ----------------
    if ((pte_kva[vpn0] & _PAGE_PRESENT) == 0) 
    {
        // alloc data page (4KB)
        uintptr_t data_page_kva = get_user_page();
        set_pfn(&pte_kva[vpn0], kva2pa(data_page_kva) >> NORMAL_PAGE_SHIFT);

        set_attribute(&pte_kva[vpn0], 
            _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC | 
            _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY);
        list_add_page(kva2pa(data_page_kva), &pte_kva[vpn0]);
        return data_page_kva; 
    }

    // mapping exists
    return pa2kva(get_pa(pte_kva[vpn0]));
}

static void _recycle_walker(PTE *entry, int level)
{
    if (!(*entry & _PAGE_PRESENT)) return;  // not valid
    uintptr_t next_kva = pa2kva(get_pa(*entry));

    if ((level==0))
    {   // is leaf node
        if (*entry & _PAGE_USER) // only is user_page recycle
            freePage(next_kva);
        // if kernel large page: not recycle
        return;
    }
    if (level > 0) 
    {
        PTE *child_pt = (PTE *)next_kva;
        for (int i = 0; i < NUM_PTE_ENTRY; i++) 
            _recycle_walker(&child_pt[i], level - 1);
        freePage(next_kva);
    }
}

void recycle_page_table(uintptr_t pgdir)
{
    PTE *pgdir_entry = (PTE *)pgdir;
    // only search user space!
    for (int i = 0; i < NUM_PTE_ENTRY / 2; i++) 
    {
        _recycle_walker(&pgdir_entry[i], 2); // Level 2: root
    }
}


int alloc_swap_slot() 
{   // distribute one free slot in disk
    for (int i = 0; i < SWAP_MAX_PAGES; i++) 
    {
        if (swap_disk_map[i] == 0) {
            swap_disk_map[i] = 1;
            return i;
        }
    }
    printk("Error: Out of Swap Disk Space!\n");
    return -1;
}

void free_swap_slot(int idx) 
{
    if (idx >= 0 && idx < SWAP_MAX_PAGES)
        swap_disk_map[idx] = 0;
}

void list_add_page(uintptr_t pa, PTE *pte) 
{   
    int next_tail = (swap_tail + 1) % MAX_USER_PAGES;
    swap_queue[swap_tail].pa = pa;
    swap_queue[swap_tail].pte = pte;
    swap_tail = next_tail;
}

// find first valid and swap out
ptr_t swap_out()
{
    if (swap_head == swap_tail) return 0;
    for(int k=0; k<MAX_USER_PAGES; k++) 
    {
        swap_entry_t *victim = &swap_queue[swap_head];
        swap_head = (swap_head + 1) % MAX_USER_PAGES;

        PTE pte_val = *(victim->pte);

        uintptr_t current_pa = (pte_val >> _PAGE_PFN_SHIFT) << NORMAL_PAGE_SHIFT;        
        if ( !(pte_val & _PAGE_PRESENT) || (current_pa != victim->pa) ) 
            continue;

        int swap_idx = alloc_swap_slot();
        if (swap_idx == -1)     // disk full
            return 0; 

        bios_sd_write(pa2kva(victim->pa), PAGE_SECTORS, SWAP_START_SEC + swap_idx * PAGE_SECTORS);

        pte_val &= ~_PAGE_PRESENT;  // page not valid
        pte_val &= ~PTE_PFN_MASK;   // clear pfn
        pte_val |= _PAGE_SWAP;      // set swap tag
        pte_val &= ~(_PAGE_DIRTY);  // clear dirty
        pte_val |= ((uint64_t)swap_idx << _PAGE_PFN_SHIFT); // store swap disk pos

        *(victim->pte) = pte_val;
        local_flush_tlb_all();
        ptr_t free_page_kva = pa2kva(victim->pa);
        memset((void*)free_page_kva, 0, PAGE_SIZE);
        return free_page_kva;
    }
    return 0;
}

void swap_in(uintptr_t stval, PTE *pte) 
{
    int swap_idx = (*pte & PTE_PFN_MASK) >> _PAGE_PFN_SHIFT;

    uintptr_t new_page_kva = get_user_page();
    uintptr_t new_page_pa = kva2pa(new_page_kva);

    int ret = bios_sd_read(new_page_kva, PAGE_SECTORS, SWAP_START_SEC + swap_idx * PAGE_SECTORS);
    if (ret != 0) 
    {
        printk("Error: bios_sd_read failed %d\n", ret);
        freePage(new_page_kva);
        return;
    }
    asm volatile("fence" : : : "memory");
    asm volatile("fence.i" : : : "memory");
    free_swap_slot(swap_idx);
    *pte = 0;   // clear _PAGE_SWAP and swap_pos(pfn)

    set_pfn(pte, new_page_pa >> NORMAL_PAGE_SHIFT);
    *pte |= (_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC 
            | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY);
    list_add_page(new_page_pa, pte);
    local_flush_tlb_page(stval);
}

size_t get_free_memory()
{   // return bytes count
    size_t total_free = 0;

    if (kernMemCurr < MEM_END) 
        total_free += (MEM_END - kernMemCurr);

    // ptr_t cur = freePageList;
    // while (cur != 0) 
    // {
    //     total_free += PAGE_SIZE;
    //     cur = *(ptr_t *)cur; 
    // }
    total_free += free_pages_count * PAGE_SIZE;
    return total_free;
}


/* =========================================================================
 * P4-task5: Zero Copy Pipe
 * ========================================================================= */

pipe_t pipes[MAX_PIPES];

void init_pipes() 
{
    for(int i=0; i<MAX_PIPES; i++) pipes[i].valid = 0;
}

PTE* get_pte_ptr(uintptr_t pgdir, uintptr_t va) 
{
    uint64_t vpn2 = (va >> 30) & 0x1FF;
    uint64_t vpn1 = (va >> 21) & 0x1FF;
    uint64_t vpn0 = (va >> 12) & 0x1FF;

    PTE *pgdir_kva = (PTE *)pgdir;
    if (!(pgdir_kva[vpn2] & _PAGE_PRESENT)) return NULL;

    PTE *pmd_kva = (PTE *)pa2kva(get_pa(pgdir_kva[vpn2]));
    if (!(pmd_kva[vpn1] & _PAGE_PRESENT)) return NULL;

    PTE *pte_kva = (PTE *)pa2kva(get_pa(pmd_kva[vpn1]));
    return &pte_kva[vpn0];
}

static PTE* ensure_pte_path(uintptr_t pgdir, uintptr_t va) 
{
    uint64_t vpn2 = (va >> 30) & 0x1FF;
    uint64_t vpn1 = (va >> 21) & 0x1FF;
    uint64_t vpn0 = (va >> 12) & 0x1FF;

    PTE *pgdir_kva = (PTE *)pgdir;
    // Level 2
    if (!(pgdir_kva[vpn2] & _PAGE_PRESENT)) {
        ptr_t new_page = allocPage(1); 
        if(new_page == 0) return NULL;
        set_pfn(&pgdir_kva[vpn2], kva2pa(new_page) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pgdir_kva[vpn2], _PAGE_PRESENT);
    }
    
    PTE *pmd_kva = (PTE *)pa2kva(get_pa(pgdir_kva[vpn2]));
    // Level 1
    if (!(pmd_kva[vpn1] & _PAGE_PRESENT)) {
        ptr_t new_page = allocPage(1); 
        if(new_page == 0) return NULL;
        set_pfn(&pmd_kva[vpn1], kva2pa(new_page) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pmd_kva[vpn1], _PAGE_PRESENT);
    }

    PTE *pte_kva = (PTE *)pa2kva(get_pa(pmd_kva[vpn1]));
    return &pte_kva[vpn0];
}

int do_pipe_open(const char *name) 
{
    for (int i = 0; i < MAX_PIPES; i++) 
    {
        if (pipes[i].valid && strcmp(pipes[i].name, name) == 0) return i;
    }
    for (int i = 0; i < MAX_PIPES; i++) 
    {
        if (!pipes[i].valid) 
        {
            pipes[i].valid = 1;
            strcpy(pipes[i].name, name);
            pipes[i].head = 0;
            pipes[i].tail = 0;
            pipes[i].count = 0;
            init_list_head(&pipes[i].wait_queue); 
            pipes[i].mutex = do_mutex_lock_init(PIPE_LOCK_KEY_BASE + i);
            return i;
        }
    }
    return -1;  // pipe full
}

long do_pipe_give_pages(int pipe_idx, void *src, size_t length) 
{
    if (pipe_idx < 0 || pipe_idx >= MAX_PIPES || !pipes[pipe_idx].valid) return -1;
    pipe_t *pipe = &pipes[pipe_idx];
    
    int total_pages = length / PAGE_SIZE;
    int processed = 0;
    uintptr_t current_src = (uintptr_t)src;
    do_mutex_lock_acquire(pipe->mutex);

    for (int i = 0; i < total_pages; i++) 
    {
        // screen_move_cursor(0, 7);
        // if (i % 10 == 0)
        //     printk("[Pipe Send] Processing page %d/%d, current_src=0x%lx\n", i, total_pages, current_src);

        int cpu_id = get_current_cpu_id();
        // if pipe full, block
        while (pipe->count >= PIPE_SIZE) 
        {
            do_mutex_lock_release(pipe->mutex);
            current_running[cpu_id]->status = TASK_BLOCKED;
            do_block(&current_running[cpu_id]->list, &pipe->wait_queue);
            do_scheduler();
            do_mutex_lock_acquire(pipe->mutex);
        }

        PTE *pte = get_pte_ptr(current_running[cpu_id]->pgdir, current_src);
        if (!pte || *pte == 0) 
        {
            current_src += PAGE_SIZE;
            continue;
        }

        pipe_page_t *node = &pipe->pages[pipe->tail];
        if (*pte & _PAGE_PRESENT) 
        {
            node->load_addr = (*pte >> _PAGE_PFN_SHIFT) << NORMAL_PAGE_SHIFT; // 存 PA
            node->is_swap = 0;
        } 
        else 
        {
            node->load_addr = *pte;
            node->is_swap = 1;
        }

        pipe->tail = (pipe->tail + 1) % PIPE_SIZE;
        pipe->count++;
        // unmap the page from sender
        *pte = 0; 
        local_flush_tlb_page(current_src);

        current_src += PAGE_SIZE;
        processed++;
        // wake up receiver
        do_unblock(&pipe->wait_queue);
    }
    do_mutex_lock_release(pipe->mutex);
    return processed * PAGE_SIZE;
}

long do_pipe_take_pages(int pipe_idx, void *dst, size_t length) 
{
    if (pipe_idx < 0 || pipe_idx >= MAX_PIPES || !pipes[pipe_idx].valid) return -1;
    pipe_t *pipe = &pipes[pipe_idx];

    int total_pages = length / PAGE_SIZE;
    int processed = 0;
    uintptr_t current_dst = (uintptr_t)dst;
    do_mutex_lock_acquire(pipe->mutex);

    for (int i = 0; i < total_pages; i++) 
    {
        // screen_move_cursor(0, 8);
        // if (i % 10 == 0) {
        //     printk("[Pipe Recv] Processing page %d/%d\n", i, total_pages);
        // }
        int cpu_id = get_current_cpu_id();
        while (pipe->count <= 0) {
            current_running[cpu_id]->status = TASK_BLOCKED;
            do_mutex_lock_release(pipe->mutex);
            do_block(&current_running[cpu_id]->list, &pipe->wait_queue);
            do_scheduler();
            do_mutex_lock_acquire(pipe->mutex);
        }

        pipe_page_t *node = &pipe->pages[pipe->head];
        PTE *pte = ensure_pte_path(current_running[cpu_id]->pgdir, current_dst);
        if (!pte) 
        {
            do_mutex_lock_release(pipe->mutex);
            return -1;
        }

        if (*pte & _PAGE_PRESENT) 
        {   // not swap
            uintptr_t old_pa = (*pte >> _PAGE_PFN_SHIFT) << NORMAL_PAGE_SHIFT;
            freePage(pa2kva(old_pa)); 
        }
        else 
        {   // swap
            int swap_idx = (*pte) >> _PAGE_PFN_SHIFT;
            free_swap_slot(swap_idx);
        }

        if (node->is_swap)
            *pte = node->load_addr; // get back swap info
        else 
        {
            uintptr_t pa = node->load_addr;
            set_pfn(pte, pa >> NORMAL_PAGE_SHIFT);
            set_attribute(pte, _PAGE_PRESENT | _PAGE_USER | _PAGE_READ | _PAGE_WRITE | 
                               _PAGE_EXEC | _PAGE_ACCESSED | _PAGE_DIRTY);
            list_add_page(pa, pte); 
        }

        pipe->head = (pipe->head + 1) % PIPE_SIZE;
        pipe->count--;
        local_flush_tlb_page(current_dst);

        current_dst += PAGE_SIZE;
        processed++;
        // wake up sender
        do_unblock(&pipe->wait_queue);
    }
    do_mutex_lock_release(pipe->mutex);
    return processed * PAGE_SIZE;
}


uintptr_t shm_page_get(int key)
{
    // TODO [P4-task4] shm_page_get:
}

void shm_page_dt(uintptr_t addr)
{
    // TODO [P4-task4] shm_page_dt:
}
