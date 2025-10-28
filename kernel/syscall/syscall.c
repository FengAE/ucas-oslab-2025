#include <sys/syscall.h>

long (*syscall[NUM_SYSCALLS])();

void handle_syscall(regs_context_t *regs, uint64_t interrupt, uint64_t cause)
{
    /* TODO: [p2-task3] handle syscall exception */
    /**
     * HINT: call syscall function like syscall[fn](arg0, arg1, arg2),
     * and pay attention to the return value and sepc
     */
    long fn = (long)regs->regs[17];     // a7 
    long arg0 = (long)regs->regs[10];   // a0 
    long arg1 = (long)regs->regs[11];   // a1 
    long arg2 = (long)regs->regs[12];   // a2 
    long arg3 = (long)regs->regs[13];   // a3
    long arg4 = (long)regs->regs[14];   // a4
    regs->regs[10] = (reg_t)syscall[fn](arg0, arg1, arg2, arg3, arg4);
    regs->sepc += 4;
}
