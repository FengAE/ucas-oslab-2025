#include <os/mm.h>
#include <os/string.h>
#include <os/sched.h>
#include <printk.h>
#include <screen.h>

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