#include <syscall.h>
#include <stdint.h>
#include <kernel.h>
#include <unistd.h>

static const long IGNORE = 0L;

static long invoke_syscall(long sysno, long arg0, long arg1, long arg2,
                           long arg3, long arg4)
{
    /* TODO: [p2-task3] implement invoke_syscall via inline assembly */
<<<<<<< HEAD
    long res;
    asm volatile(
        "mv     a7, %1\n"
        "mv     a0, %2\n"
        "mv     a1, %3\n"
        "mv     a2, %4\n"
        "mv     a3, %5\n"
        "mv     a4, %6\n"
        "ecall\n"
        "mv     %0, a0\n"
        :"=r"(res)
        :"r"(sysno),"r"(arg0),"r"(arg1),"r"(arg2),"r"(arg3),"r"(arg4)
    );
    return res;
=======
    asm volatile("nop");

    return 0;
>>>>>>> framework/Project4
}

void sys_yield(void)
{
    /* TODO: [p2-task1] call call_jmptab to implement sys_yield */
<<<<<<< HEAD
    // call_jmptab(YIELD, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
    /* TODO: [p2-task3] call invoke_syscall to implement sys_yield */
    invoke_syscall(SYSCALL_YIELD, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
=======
    /* TODO: [p2-task3] call invoke_syscall to implement sys_yield */
>>>>>>> framework/Project4
}

void sys_move_cursor(int x, int y)
{
    /* TODO: [p2-task1] call call_jmptab to implement sys_move_cursor */
<<<<<<< HEAD
    // call_jmptab(MOVE_CURSOR, x, y, IGNORE, IGNORE, IGNORE);
    /* TODO: [p2-task3] call invoke_syscall to implement sys_move_cursor */
    invoke_syscall(SYSCALL_CURSOR, x, y, IGNORE, IGNORE, IGNORE);
=======
    /* TODO: [p2-task3] call invoke_syscall to implement sys_move_cursor */
>>>>>>> framework/Project4
}

void sys_write(char *buff)
{
    /* TODO: [p2-task1] call call_jmptab to implement sys_write */
<<<<<<< HEAD
    // call_jmptab(WRITE, (long)buff, IGNORE, IGNORE, IGNORE, IGNORE);
    /* TODO: [p2-task3] call invoke_syscall to implement sys_write */
    invoke_syscall(SYSCALL_WRITE, (long)buff, IGNORE, IGNORE, IGNORE, IGNORE);
=======
    /* TODO: [p2-task3] call invoke_syscall to implement sys_write */
>>>>>>> framework/Project4
}

void sys_reflush(void)
{
    /* TODO: [p2-task1] call call_jmptab to implement sys_reflush */
<<<<<<< HEAD
    // call_jmptab(REFLUSH, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
    /* TODO: [p2-task3] call invoke_syscall to implement sys_reflush */
    invoke_syscall(SYSCALL_REFLUSH, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
=======
    /* TODO: [p2-task3] call invoke_syscall to implement sys_reflush */
>>>>>>> framework/Project4
}

int sys_mutex_init(int key)
{
    /* TODO: [p2-task2] call call_jmptab to implement sys_mutex_init */
<<<<<<< HEAD
    // call_jmptab(MUTEX_INIT, key, IGNORE, IGNORE, IGNORE, IGNORE);
    /* TODO: [p2-task3] call invoke_syscall to implement sys_mutex_init */
     return invoke_syscall(SYSCALL_LOCK_INIT, key, IGNORE, IGNORE, IGNORE, IGNORE);
=======
    /* TODO: [p2-task3] call invoke_syscall to implement sys_mutex_init */
    return 0;
>>>>>>> framework/Project4
}

void sys_mutex_acquire(int mutex_idx)
{
    /* TODO: [p2-task2] call call_jmptab to implement sys_mutex_acquire */
<<<<<<< HEAD
    // call_jmptab(MUTEX_ACQ, mutex_idx, IGNORE, IGNORE, IGNORE, IGNORE);
    /* TODO: [p2-task3] call invoke_syscall to implement sys_mutex_acquire */
    invoke_syscall(SYSCALL_LOCK_ACQ, (long)mutex_idx, IGNORE, IGNORE, IGNORE, IGNORE);
=======
    /* TODO: [p2-task3] call invoke_syscall to implement sys_mutex_acquire */
>>>>>>> framework/Project4
}

void sys_mutex_release(int mutex_idx)
{
    /* TODO: [p2-task2] call call_jmptab to implement sys_mutex_release */
<<<<<<< HEAD
    // call_jmptab(MUTEX_RELEASE, mutex_idx, IGNORE, IGNORE, IGNORE, IGNORE);
    /* TODO: [p2-task3] call invoke_syscall to implement sys_mutex_release */
    invoke_syscall(SYSCALL_LOCK_RELEASE, (long)mutex_idx, IGNORE, IGNORE, IGNORE, IGNORE);
=======
    /* TODO: [p2-task3] call invoke_syscall to implement sys_mutex_release */
>>>>>>> framework/Project4
}

long sys_get_timebase(void)
{
    /* TODO: [p2-task3] call invoke_syscall to implement sys_get_timebase */
<<<<<<< HEAD
    return invoke_syscall(SYSCALL_GET_TIMEBASE, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
=======
    return 0;
>>>>>>> framework/Project4
}

long sys_get_tick(void)
{
    /* TODO: [p2-task3] call invoke_syscall to implement sys_get_tick */
<<<<<<< HEAD
    return invoke_syscall(SYSCALL_GET_TICK, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
=======
    return 0;
>>>>>>> framework/Project4
}

void sys_sleep(uint32_t time)
{
    /* TODO: [p2-task3] call invoke_syscall to implement sys_sleep */
<<<<<<< HEAD
    invoke_syscall(SYSCALL_SLEEP, (long)time, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_set_sche_workload(int workload)
{
    /* Report current workload to scheduler for dynamic scheduling */
    invoke_syscall(SYSCALL_SET_SCHED_WORKLOAD, (long)workload, IGNORE, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

/************************************************************/
#ifdef S_CORE
pid_t  sys_exec(int id, int argc, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_exec for S_CORE */
<<<<<<< HEAD
    return invoke_syscall(SYSCALL_EXEC, id, argc, arg0, arg1, arg2);
}    
#else
pid_t sys_exec(char *name, int argc, char **argv)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_exec */
    return invoke_syscall(SYSCALL_EXEC, (long)name, argc, (long)argv, IGNORE, IGNORE);
}
#endif

pid_t sys_taskset(void* name, int mask, int mode)
{
    return invoke_syscall(SYSCALL_TASKSET, (long)name, mask, mode, IGNORE, IGNORE);
}

void sys_exit(void)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_exit */
    invoke_syscall(SYSCALL_EXIT, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
=======
}    
#else
pid_t  sys_exec(char *name, int argc, char **argv)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_exec */
}
#endif

void sys_exit(void)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_exit */
>>>>>>> framework/Project4
}

int  sys_kill(pid_t pid)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_kill */
<<<<<<< HEAD
    return invoke_syscall(SYSCALL_KILL, pid, IGNORE, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

int  sys_waitpid(pid_t pid)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_waitpid */
<<<<<<< HEAD
    return invoke_syscall(SYSCALL_WAITPID, pid, IGNORE, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}


void sys_ps(void)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_ps */
<<<<<<< HEAD
    invoke_syscall(SYSCALL_PS, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

pid_t sys_getpid()
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_getpid */
<<<<<<< HEAD
    return invoke_syscall(SYSCALL_GETPID, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

int  sys_getchar(void)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_getchar */
<<<<<<< HEAD
    return invoke_syscall(SYSCALL_READCH, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

int  sys_barrier_init(int key, int goal)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_barrier_init */
<<<<<<< HEAD
    return invoke_syscall(SYSCALL_BARR_INIT, key, goal, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

void sys_barrier_wait(int bar_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_barrie_wait */
<<<<<<< HEAD
    invoke_syscall(SYSCALL_BARR_WAIT, bar_idx, IGNORE, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

void sys_barrier_destroy(int bar_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_barrie_destory */
<<<<<<< HEAD
    invoke_syscall(SYSCALL_BARR_DESTROY, bar_idx, IGNORE, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

int sys_condition_init(int key)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_condition_init */
<<<<<<< HEAD
    return invoke_syscall(SYSCALL_COND_INIT, key, IGNORE, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

void sys_condition_wait(int cond_idx, int mutex_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_condition_wait */
<<<<<<< HEAD
    invoke_syscall(SYSCALL_COND_WAIT, cond_idx, mutex_idx, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

void sys_condition_signal(int cond_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_condition_signal */
<<<<<<< HEAD
    invoke_syscall(SYSCALL_COND_SIGNAL, cond_idx, IGNORE, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

void sys_condition_broadcast(int cond_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_condition_broadcast */
<<<<<<< HEAD
    invoke_syscall(SYSCALL_COND_BROADCAST, cond_idx, IGNORE, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

void sys_condition_destroy(int cond_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_condition_destroy */
<<<<<<< HEAD
    invoke_syscall(SYSCALL_COND_DESTROY, cond_idx, IGNORE, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

int sys_semaphore_init(int key, int init)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_semaphore_init */
<<<<<<< HEAD
    return invoke_syscall(SYSCALL_SEMA_INIT, key, init, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

void sys_semaphore_up(int sema_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_semaphore_up */
}

void sys_semaphore_down(int sema_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_semaphore_down */
}

void sys_semaphore_destroy(int sema_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_semaphore_destroy */
}

int sys_mbox_open(char * name)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_mbox_open */
<<<<<<< HEAD
    return invoke_syscall(SYSCALL_MBOX_OPEN, (long)name, IGNORE, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

void sys_mbox_close(int mbox_id)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_mbox_close */
<<<<<<< HEAD
    invoke_syscall(SYSCALL_MBOX_CLOSE, mbox_id, IGNORE, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

int sys_mbox_send(int mbox_idx, void *msg, int msg_length)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_mbox_send */
<<<<<<< HEAD
    return invoke_syscall(SYSCALL_MBOX_SEND, mbox_idx, (long)msg, msg_length, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

int sys_mbox_recv(int mbox_idx, void *msg, int msg_length)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_mbox_recv */
<<<<<<< HEAD
    return invoke_syscall(SYSCALL_MBOX_RECV, mbox_idx, (long)msg, msg_length, IGNORE, IGNORE);
}

// I/O 
void sys_clear(int line1, int line2)
{
    invoke_syscall(SYSCALL_CLEAR, line1, line2, IGNORE, IGNORE, IGNORE);
}

// Thread
void sys_thread_create(int* thread_id, void *func, void* arg)
{
    invoke_syscall(SYSCALL_THREAD_CREATE, (long)thread_id, (long)func, (long)arg, IGNORE, IGNORE);
}

void sys_thread_exit(void)
{
    invoke_syscall(SYSCALL_THREAD_EXIT, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}
void sys_thread_join(pid_t pid)
{
    invoke_syscall(SYSCALL_THREAD_JOIN, pid, IGNORE, IGNORE, IGNORE, IGNORE);
=======
>>>>>>> framework/Project4
}

/************************************************************/
