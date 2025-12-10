#include <os/irq.h>
#include <os/time.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/kernel.h>
#include <os/mm.h>
#include <printk.h>
#include <assert.h>
#include <screen.h>
#include <csr.h>

handler_t irq_table[IRQC_COUNT];
handler_t exc_table[EXCC_COUNT];

void interrupt_helper(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p2-task3] & [p2-task4] interrupt handler.
    // call corresponding handler by the value of `scause`
    if((scause>>63) & 1)    // interrupt
        irq_table[scause & ~(1ULL<<63)](regs, stval, scause);
    else    // exception
        exc_table[scause](regs, stval, scause);
}

void handle_irq_soft(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // clear soft interrupts' bit
    // to avoid interrupt pending
    asm volatile("csrc sip, %0" : : "r" (SIE_SSIE));
}


void handle_page_fault(regs_context_t *regs, uint64_t stval, uint64_t scause)
{    
    if (stval >= 0x4000000000ULL) 
    { // exceed sv39 user address upbound
        printk("Segfault: Illegal access to 0x%lx\n", stval);
        do_exit(); 
        return;
    }
    int cpu_id = get_current_cpu_id();
    uintptr_t pgdir = current_running[cpu_id]->pgdir;
    PTE* pte = get_pte_ptr(pgdir, stval);
    PTE pte_val = pte ? *pte : 0;
    if (pte_val & _PAGE_PRESENT) 
    {
        int need_flush = 0;
        switch (scause) {
            case EXCC_INST_PAGE_FAULT:
                if (pte_val & _PAGE_EXEC) need_flush = 1;
                break;        
            case EXCC_LOAD_PAGE_FAULT:
                if (pte_val & _PAGE_READ) need_flush = 1;
                break;               
            case EXCC_STORE_PAGE_FAULT:
                if (pte_val & _PAGE_WRITE)  need_flush = 1;
                break;
        }
        if (need_flush) 
        {   // multi-core: core 0 flush stval->valid, but core 1 not flush cause page fault
            local_flush_tlb_page(stval);
            return;
        }
    }

    if (pte && (*pte & _PAGE_SWAP))
        swap_in(stval, pte);
    else 
    {   // Page loss
        alloc_page_helper(stval, pgdir);
        local_flush_tlb_page(stval);
    }
    
}

void handle_irq_timer(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p2-task4] clock interrupt handler.
    // Note: use bios_set_timer to reset the timer and remember to reschedule
    set_timer(get_ticks() + TIMER_INTERVAL);    // Supervisor mode

    // if(current_running != NULL && current_running->status == TASK_RUNNING)
    // {
    //     if(current_running->time_slice_remaining > 0)
    //         current_running->time_slice_remaining--;
    // }

    do_scheduler();
}

void init_exception()
{
    /* TODO: [p2-task3] initialize exc_table */
    /* NOTE: handle_syscall, handle_other, etc.*/
    for(int i=0; i<EXCC_COUNT; i++)
        exc_table[i] = handle_other;
    exc_table[EXCC_SYSCALL] = handle_syscall;
    exc_table[EXCC_LOAD_PAGE_FAULT] = handle_page_fault;
    exc_table[EXCC_STORE_PAGE_FAULT] = handle_page_fault;
    exc_table[EXCC_INST_PAGE_FAULT]  = handle_page_fault;

    /* TODO: [p2-task4] initialize irq_table */
    /* NOTE: handle_int, handle_other, etc.*/
    for(int i=0; i<IRQC_COUNT; i++)
        irq_table[i] = handle_other;
    irq_table[IRQC_S_TIMER] = handle_irq_timer;
    irq_table[IRQC_S_SOFT]  = handle_irq_soft;

    /* TODO: [p2-task3] set up the entrypoint of exceptions */
    setup_exception();
}

void handle_other(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    char* reg_name[] = {
        "zero "," ra  "," sp  "," gp  "," tp  ",
        " t0  "," t1  "," t2  ","s0/fp"," s1  ",
        " a0  "," a1  "," a2  "," a3  "," a4  ",
        " a5  "," a6  "," a7  "," s2  "," s3  ",
        " s4  "," s5  "," s6  "," s7  "," s8  ",
        " s9  "," s10 "," s11 "," t3  "," t4  ",
        " t5  "," t6  "
    };
    for (int i = 0; i < 32; i += 3) {
        for (int j = 0; j < 3 && i + j < 32; ++j) {
            printk("%s : %016lx ",reg_name[i+j], regs->regs[i+j]);
        }
        printk("\n\r");
    }
    printk("sstatus: 0x%lx sbadaddr: 0x%lx scause: %lu\n\r",
           regs->sstatus, regs->sbadaddr, regs->scause);
    printk("sepc: 0x%lx\n\r", regs->sepc);
    printk("tval: 0x%lx cause: 0x%lx\n", stval, scause);
    assert(0);
}
