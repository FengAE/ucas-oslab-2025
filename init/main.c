#include <common.h>
#include <asm.h>
#include <asm/unistd.h>
#include <os/loader.h>
#include <os/irq.h>
#include <os/sched.h>
#include <os/lock.h>
#include <os/kernel.h>
#include <os/task.h>
#include <os/string.h>
#include <os/mm.h>
#include <os/list.h>
#include <os/time.h>
#include <sys/syscall.h>
#include <screen.h>
#include <printk.h>
#include <assert.h>
#include <type.h>
#include <csr.h>

extern void ret_from_exception();
#define VERSION_BUF 50
#define TASK_INFO_LOC 0x50200200
#define TASK_NUM_LOC 0x502001fe
#define TASK_INFO_START_LOC 0x502001f8
#define BATCH_START_SEC_LOC 0x502001f4
#define BUFFER 0x59000000

// --------------- [p1-task5] ------------------
// The same revise in /tiny_libc/include
#define BATCH_DATA_LOC 0x59100000
// ---------------------------------------------

int version = 2; // version must between 0 and 9
char buf[VERSION_BUF];
int tasknum;

// Task info array
task_info_t tasks[TASK_MAXNUM];

static batch_file_t batchfiles;
static void Backspace(int* name_ptr);

static int bss_check(void)
{
    for (int i = 0; i < VERSION_BUF; ++i)
    {
        if (buf[i] != 0)
        {
            return 0;
        }
    }
    return 1;
}

static void init_jmptab(void)
{
    volatile long (*(*jmptab))() = (volatile long (*(*))())KERNEL_JMPTAB_BASE;

    jmptab[CONSOLE_PUTSTR]  = (long (*)())port_write;
    jmptab[CONSOLE_PUTCHAR] = (long (*)())port_write_ch;
    jmptab[CONSOLE_GETCHAR] = (long (*)())port_read_ch;
    jmptab[SD_READ]         = (long (*)())sd_read;
    jmptab[SD_WRITE]        = (long (*)())sd_write;
    jmptab[QEMU_LOGGING]    = (long (*)())qemu_logging;
    jmptab[SET_TIMER]       = (long (*)())set_timer;
    jmptab[READ_FDT]        = (long (*)())read_fdt;
    jmptab[MOVE_CURSOR]     = (long (*)())screen_move_cursor;
    jmptab[PRINT]           = (long (*)())printk;
    jmptab[YIELD]           = (long (*)())do_scheduler;
    jmptab[MUTEX_INIT]      = (long (*)())do_mutex_lock_init;
    jmptab[MUTEX_ACQ]       = (long (*)())do_mutex_lock_acquire;
    jmptab[MUTEX_RELEASE]   = (long (*)())do_mutex_lock_release;

    // TODO: [p2-task1] (S-core) initialize system call table.
    jmptab[WRITE]           = (volatile long (*)())screen_write;
    jmptab[REFLUSH]         = (volatile long (*)())screen_reflush;
}

static void init_task_info(void)
{
    // TODO: [p1-task4] Init 'tasks' array via reading app-info sector
    // NOTE: You need to get some related arguments from bootblock first
    tasknum = *((short*)TASK_NUM_LOC);  // bootblock stage load: image->memory
    int task_info_start_sec = *((int*)TASK_INFO_START_LOC);

    unsigned sec_num = (unsigned)NBYTES2SEC(sizeof(task_info_t)*tasknum);
    bios_sd_read((unsigned int)tasks, sec_num, task_info_start_sec);
}

static void load_batchfiles()
{
    int batch_start_sec = *((int*)BATCH_START_SEC_LOC);
    int ch, name_ptr = 0;
    char name[16];
    batchfiles.num = 0;
    while(1)
    {
        while((ch = bios_getchar()) == -1);
        if(ch == '\r' || ch == '\n')
        {
            if(name_ptr != 0)
            {
                name[name_ptr] = '\0';
                strcpy(batchfiles.names[batchfiles.num++], name);
                name_ptr = 0;
            }
            else
            {
                bios_putstr("Input empty, finish loading batch files");
            }
            bios_putstr("\n\r");
            break;
        }
        else
        {
            if(ch == '\b' || ch == 127)
            {
                Backspace(&name_ptr);
                continue;
            }
            bios_putchar(ch);
            if(ch == ' ')
            {
                if(name_ptr == 0)
                    continue;
                else 
                {
                    name[name_ptr] = '\0';
                    if(batchfiles.num >= BATCH_MAXNUM)
                    {
                        bios_putstr("batch file num exceed max limit\n\r");
                        batchfiles.num = 0;
                        return;
                    }
                    strcpy(batchfiles.names[batchfiles.num++], name);
                    name_ptr = 0;
                }
            }
            else 
            {
                if(name_ptr >= 16)
                {
                    bios_putstr("\n\r");
                    bios_putstr("input task name too long\n\r");
                    name_ptr = 0;
                }
                else
                    name[name_ptr++] = ch;
            }
        }
    }
    bios_sd_write((uint64_t)&batchfiles, 1, batch_start_sec);       
}

static void read_batchfiles()
{
    int batch_start_sec = *((int*)BATCH_START_SEC_LOC);
    bios_sd_read((unsigned int)BUFFER, 1, batch_start_sec);
    memcpy((void*)&batchfiles, (void*)BUFFER, sizeof(batch_file_t));
}

static void excute_batchfiles(task_info_t* tasks, int tasknum)
{
    if(tasknum == 0)
    {
        bios_putstr("No batch files loaded!\n\r");
        return;
    }
    read_batchfiles();

    // Need to load begin data from console
    int ch, data = 0;
    bios_putstr("Please input data the first batch file get:\n\r");
    while(1)
    {
        while((ch = bios_getchar()) == -1);
        if(ch == '\n' || ch == '\r')    
        {
            bios_putstr("\n\r");
            break;
        }
        else if(ch == '\b' || ch == 127)
        {
            if(data > 0)
            {
                data /=10;
                bios_putstr("\b \b");
            }
            continue;
        }
        bios_putchar(ch);
        if(ch >= '0' && ch <= '9')
            data *= 10, data += ch-'0';
        else
        {
            bios_putstr("Input not valid, expecting number 0~9\n\r");
            data = 0;
            continue;
        }
    }
    *(int*)BATCH_DATA_LOC = data;
    // Excute batch file
    for(int i=0; i<batchfiles.num; i++)
    {
        bios_putstr("excute batch file: ");
        bios_putstr(batchfiles.names[i]);
        bios_putstr("\n\r");
        excute_by_name(batchfiles.names[i], tasks, tasknum, true);
    }
}

static void Backspace(int* name_ptr)
{
    if(*name_ptr > 0)
    {
        (*name_ptr)--;
        bios_putstr("\b \b");
    }
}

void list_files()
{
    bios_putstr("(loaded apps): \n\r");
    if(tasknum == 0)    bios_putstr("No app files\n\r");
    else{
        for(int i=0; i<tasknum; i++)
        {
            bios_putstr(tasks[i].name);
            bios_putstr("\n\r");
        }
    }
    bios_putstr("(batch files):\n\r");
    if(batchfiles.num == 0)
    {
        bios_putstr("No batch files loaded!\n\r");
    }
    else{
        for(int i=0; i<batchfiles.num; i++)
        {
            bios_putstr(batchfiles.names[i]);
            bios_putstr("\n\r");
        }
    }
    bios_putstr("\n\r");
}

/************************************************************/
void init_pcb_stack(
    ptr_t kernel_stack, ptr_t user_stack, ptr_t entry_point,
    pcb_t *pcb)
{
     /* TODO: [p2-task3] initialization of registers on kernel stack
      * HINT: sp, ra, sepc, sstatus
      * NOTE: To run the task in user mode, you should set corresponding bits
      *     of sstatus(SPP, SPIE, etc.).
      */
    regs_context_t *pt_regs =
        (regs_context_t *)(kernel_stack - sizeof(regs_context_t));
    memset(pt_regs, 0, sizeof(regs_context_t));
    // sbadaddr、scause ?
    pt_regs->sepc = entry_point;
    pt_regs->sstatus = SR_SPIE & ~SR_SPP;   // ensure not modify other bits!!
    pt_regs->regs[2] = user_stack;  // user: sp
    pt_regs->regs[4] = (reg_t) pcb; // tp 

    /* TODO: [p2-task1] set sp to simulate just returning from switch_to
     * NOTE: you should prepare a stack, and push some values to
     * simulate a callee-saved context.
     */
    switchto_context_t *pt_switchto =
        (switchto_context_t *)((ptr_t)pt_regs - sizeof(switchto_context_t));
    pcb->kernel_sp = (reg_t)pt_switchto; 
    pcb->user_sp = user_stack;
    pt_switchto->regs[0] = (reg_t)ret_from_exception;   // ra
    // pt_switchto->regs[0] = (reg_t)entry_point;     // ra        
    pt_switchto->regs[1] = (reg_t)pt_switchto;  // kernel: sp
}

int pcb_num;
static void init_pcb(void)
{
    pcb_num = 0;
    /* TODO: [p2-task1] load needed tasks and init their corresponding PCB */
    pid0_pcb.pid = 0;
    pid0_pcb.status = TASK_RUNNING;
    pid0_pcb.kernel_sp = allocKernelPage(1);
    pid0_pcb.user_sp = 0;
    pid0_pcb.cursor_x = pid0_pcb.cursor_y = 0;
    init_pcb_stack(pid0_pcb.kernel_sp, pid0_pcb.user_sp, 0, &pid0_pcb);

    for (int i = 0; i < NUM_MAX_TASK; i++) 
    {
        pcb[i].pid = 0; 
        pcb[i].status = TASK_EXITED; // free, exec can use
        pcb[i].check_point = 0;
        pcb[i].workload = 0;
        pcb[i].name = "\0";
    }
    /* TODO: [p2-task1] remember to initialize 'current_runing' */
    current_running = &pid0_pcb;

    char* argv[1] = {"shell"};
    pid_t shell_pid = do_exec("shell", 1, argv);
    // char* argv1[1] = {"waitpid"};
    // do_exec("waitpid", 1, argv1);
}

static void init_syscall(void)
{
    // TODO: [p2-task3] initialize system call table.
    syscall[SYSCALL_SLEEP] = (long (*)())do_sleep;
    syscall[SYSCALL_YIELD] = (long (*)())do_scheduler;
    syscall[SYSCALL_WRITE] = (long (*)())screen_write;
    syscall[SYSCALL_CURSOR] = (long (*)())screen_move_cursor;
    syscall[SYSCALL_REFLUSH] = (long (*)())screen_reflush;
    syscall[SYSCALL_GET_TIMEBASE] = (long (*)())get_time_base;
    syscall[SYSCALL_GET_TICK] = (long (*)())get_ticks;
    syscall[SYSCALL_LOCK_INIT] = (long (*)())do_mutex_lock_init;
    syscall[SYSCALL_LOCK_ACQ] = (long (*)())do_mutex_lock_acquire;
    syscall[SYSCALL_LOCK_RELEASE] = (long (*)())do_mutex_lock_release;
    syscall[SYSCALL_SET_SCHED_WORKLOAD] = (long (*)())set_sched_workload;
    syscall[SYSCALL_EXEC] = (long (*)())do_exec;
    syscall[SYSCALL_EXIT] = (long (*)())do_exit;
    syscall[SYSCALL_KILL] = (long (*)())do_kill;
    syscall[SYSCALL_WAITPID] = (long (*)())do_waitpid;
    syscall[SYSCALL_PS] = (long (*)())do_process_show;
    syscall[SYSCALL_GETPID] = (long (*)())do_getpid;
    syscall[SYSCALL_CLEAR] = (long (*)())screen_clear;
    syscall[SYSCALL_READCH] = (long (*)())port_read_ch;
}
/************************************************************/

int main(void)
{
    // Init jump table provided by kernel and bios(ΦωΦ)
    init_jmptab();

    // Init task information (〃'▽'〃)
    init_task_info();

    // Output 'Hello OS!', bss check result and OS version
    int check = bss_check();
    char output_str[] = "bss check: _ version: _\n\r";
    char output_val[2] = {0};
    int i, output_val_pos = 0;

    output_val[0] = check ? 't' : 'f';
    output_val[1] = version + '0';
    for (i = 0; i < sizeof(output_str); ++i)
    {
        buf[i] = output_str[i];
        if (buf[i] == '_')
        {
            buf[i] = output_val[output_val_pos++];
        }
    }
    bios_putstr("Hello OS!\n\r");
    bios_putstr(buf);

    // Init Process Control Blocks |•'-'•) ✧
    init_pcb();
    printk("> [INIT] PCB initialization succeeded.\n");
    // Read CPU frequency (｡•ᴗ-)_
    time_base = bios_read_fdt(TIMEBASE);

    // Init lock mechanism o(´^｀)o
    init_locks();
    printk("> [INIT] Lock mechanism initialization succeeded.\n");

    // Init interrupt (^_^)
    init_exception();
    printk("> [INIT] Interrupt processing initialization succeeded.\n");

    // Init system call table (0_0)n
    init_syscall();
    printk("> [INIT] System call initialized successfully.\n");

    // Init screen (QAQ)
    init_screen();   
    // printk("> [INIT] SCREEN initialization succeeded.\n");

    // printk("tasknum: %d\n", tasknum);
    // for(int i=0; i<tasknum; i++)
    // {
    //     printk("%s\n", tasks[i].name);
    // }

    // TODO: [p2-task4] Setup timer interrupt and enable all interrupt globally
    // NOTE: The function of sstatus.sie is different from sie's
    set_timer(get_ticks() + TIMER_INTERVAL);

    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    while (1)
    {
        // If you do non-preemptive scheduling, it's used to surrender control
        // do_scheduler();

        // If you do preemptive scheduling, they're used to enable CSR_SIE and wfi
        enable_preempt();
        
        // enable_time_preempt(); // We only need enable time interrupt in [p2-task4]
        asm volatile("wfi");
    }

    return 0;
}

