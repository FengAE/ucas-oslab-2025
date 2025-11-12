## Task1&2

### switch_to函数

切换上下文、保存寄存器

```asm
sd   ra, SWITCH_TO_RA(a0)
...
ld    ra, SWITCH_TO_RA(a1)
```



### PCB初始化

```
0x50501000 ← pid0_stack
    ├──────────────┤
    │  空闲栈空间 │
    │              │ ← pid0_pcb.kernel_sp 指向此处
    │ switch_to上下文 │
    │ regs_context │
    └──────────────┘
0x50502000
```

switch_context：保存各个caller-saved寄存器，初始化假现场只需要保存ra, sp，确保能正确跳转即可

```c
static void init_pcb_stack(
    ptr_t kernel_stack, ptr_t user_stack, ptr_t entry_point,
    pcb_t *pcb)
{
    regs_context_t *pt_regs =
        (regs_context_t *)(kernel_stack - sizeof(regs_context_t));
    memset(pt_regs, 0, sizeof(regs_context_t));
    pt_regs->sepc = entry_point;
    pt_regs->sstatus = (reg_t)SR_SPIE;
    pt_regs->regs[2] = user_stack;  // sp

    switchto_context_t *pt_switchto =
        (switchto_context_t *)((ptr_t)pt_regs - sizeof(switchto_context_t));
    pcb->kernel_sp = (reg_t)pt_switchto; 
    pcb->user_sp = user_stack;
    pt_switchto->regs[0] = (reg_t)entry_point;     // ra        
    pt_switchto->regs[1] = user_stack;  // sp
}


static void init_pcb(void)
{
    /* TODO: [p2-task1] load needed tasks and init their corresponding PCB */
    char pcb_test_tasks[][16] = {"fly", "print1", "print2"};
    int pcb_test_num = sizeof(pcb_test_tasks) / sizeof(pcb_test_tasks[0]);
	// 设置pid0_pcb
    pid0_pcb.pid = 0;
    pid0_pcb.status = TASK_RUNNING;
    pid0_pcb.kernel_sp = allocKernelPage(1);
    pid0_pcb.user_sp = 0;
    init_pcb_stack(pid0_pcb.kernel_sp, pid0_pcb.user_sp, 0, &pid0_pcb);
	// 载入各个pcb
    for(int i=0; i<pcb_test_num; i++)
    {
        pcb[i].pid = i+1;
        pcb[i].entry = load_task_img(pcb_test_tasks[i], tasks, tasknum);
        init_pcb_stack(allocKernelPage(1), allocUserPage(1),
                        pcb[i].entry, &pcb[i]);
        pcb[i].status = TASK_READY;
        pcb[i].wakeup_time = 0;
        queue_pushback(&ready_queue, &(pcb[i].list));
    }

    /* TODO: [p2-task1] remember to initialize 'current_running' */
    current_running = &pid0_pcb;
}
```





### do_scheduler

依次执行ready_queue里面的任务

```c
void do_scheduler(void)
{
    if(current_running->status == TASK_READY)
    {
        current_running->status = TASK_RUNNING;
    }
    else if(current_running->status == TASK_RUNNING)
    {
        current_running->status = TASK_READY;
        if(queue_empty(&ready_queue))   return;
        pcb_t* prev_running = current_running;
        move_next(&ready_queue);
        current_running = (pcb_t*)((char*)cur_ready- 16);
        current_running->status = TASK_RUNNING;
        switch_to(prev_running->kernel_sp, current_running->kernel_sp);
    }
}
```







### 进程阻塞与唤醒

| 状态                   | 操作           | 放入的队列          | 调用函数       |
| ---------------------- | -------------- | ------------------- | -------------- |
| 获得锁（成功）         | 占用资源       | 无需排队            | ——             |
| 获取锁失败（锁被占用） | 阻塞自己       | 锁的 `block_queue`  | `do_block()`   |
| 锁释放时               | 唤醒一个等待者 | 移回 `ready_queue`  | `do_unblock()` |
| 运行结束               | 等待回收       | 不再放入 ready 队列 | ——             |
|                        |                |                     |                |

1. 该进程状态设置为BLOCKED
2. 放入阻塞队列

```  c
void do_block(list_node_t *pcb_node, list_head *queue)
{
    queue_pushback(pcb_node, queue);
    current_running->status = TASK_BLOCKED;
    do_scheduler();
}
```

3. 等待唤醒

```c
void do_unblock(list_node_t *pcb_node)
{
    pcb_t *pcb = LIST2PCB(pcb_node);
    queue_popfront(pcb_node);
    pcb->status = TASK_READY;
    queue_pushback(&ready_queue, &pcb->list);
}
```



整个流程：

```c
void do_sleep(uint32_t sleep_time)
{
    current_running->wakeup_time = get_timer() + sleep_time;
    do_block(&current_running->list, &sleep_queue);
}
void check_sleeping()
{
    list_for_each_safe(&sleep_queue, pcb_node)
    {
        pcb_t *pcb = LIST2PCB(pcb_node);
        if (get_timer() >= pcb->wakeup_time)
            do_unblock(&pcb->list);
    }
}
```

## Task3&4

### syscall流程

1. 用户程序调用sys_call，**根据系统调用号执行对应的invoke_syscall**

```c
void sys_set_sche_workload(int workload)
{
    /* Report current workload to scheduler for dynamic scheduling */
    invoke_syscall(SYSCALL_SET_SCHED_WORKLOAD, (long)workload, IGNORE, IGNORE, IGNORE, IGNORE);
}

static long invoke_syscall(long sysno, long arg0, long arg1, long arg2, long arg3, long arg4)
{
    /* TODO: [p2-task3] implement invoke_syscall via inline assembly */
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
}
```



2. ecall指令进行异常处理，进入到stvec内部存储的exception_handler_entry，进行对应的上下文保存

```
mv    a0, sp        
csrr  a1, CSR_STVAL
csrr  a2, CSR_SCAUSE    
call  interrupt_helper
```

在进行异常处理前，先传入对应处理的stval和scause



3. 在irq.c内部，根据**scause区分中断和异常**，并执行对应函数表当中的函数

```c
void interrupt_helper(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p2-task3] & [p2-task4] interrupt handler.
    // call corresponding handler by the value of `scause`
    if((scause>>63) & 1)    // interrupt
        irq_table[scause & ~(1ULL<<63)](regs, stval, scause);
    else    // exception
        exc_table[scause](regs, stval, scause);
}
```

由于系统调用对应的函数为 `exc_table[EXCC_SYSCALL] = handle_syscall;`，故而此时执行handle_syscall

```c
void handle_syscall(regs_context_t *regs, uint64_t interrupt, uint64_t cause)
{
    /* TODO: [p2-task3] handle syscall exception */
    /**
     * HINT: call syscall function like syscall[fn](arg0, arg1, arg2),
     * and pay attention to the return value and sepc
     */
     long (*func)(long, long, long) = (long (*)(long, long, long))syscall[regs->regs[17]];	// 在main里面进行了初始化
     long value = func(regs->regs[10], regs->regs[11], regs->regs[12]);
     
     regs->regs[10] = value;
     
     regs->sepc += 4;
}
```



### 中断设置寄存器

1. 本实验采用Direct模式，需要在setup_exception设置stvec，此后发生异常与中断都跳转到该地址

```asm
  /* TODO: [p2-task3] save exception_handler_entry into STVEC */
  la    t0, exception_handler_entry   
  csrw  CSR_STVEC, t0   # set stvec（Direct mode）
```

2. 开启全局中断，即：设置sstatus.SIE=1

```asm
call  enable_interrupt

ENTRY(enable_interrupt)
  li t0, SR_SIE
  csrs CSR_SSTATUS, t0
  jr ra
```

3. 在task4中，还引入了时钟中断，故而需要解除时钟中断的屏蔽

本实验可以**直接调用enable_preempt打开所有全局中断**，但是为了更贴合实验，我采用了**只打开时钟中断的enable_time_preempt**

```asm
ENTRY(enable_preempt)
  not t0, x0
  csrs CSR_SIE, t0
  jr ra
ENDPROC(enable_preempt)

# -------- Only open Supervisor Timer Interrupt -----------
ENTRY(enable_time_preempt)
  li t0, SIE_STIE        
  csrs CSR_SIE, t0
  jr ra
ENDPROC(enable_time_preempt)
```





### 异常保存上下文

1. 刚进入SAVE_CONTEXT时，为USER态，此时栈顶为用户栈栈顶，**需要转为内核栈栈顶**访问此前存放的各种context信息

```asm
sd    sp, PCB_USER_SP(tp)   # store user_sp
  ld    sp, PCB_KERNEL_SP(tp) # change to kernel_sp
  # create space
  addi  sp, sp, -OFFSET_SIZE
```

2. 直接保存32个基本寄存器即可，并且还要保存csr寄存器

```asm
sd    t0,   OFFSET_REG_T0(sp)
...

csrr  t1,   CSR_SSTATUS
sd    t1,   OFFSET_REG_SSTATUS(sp)
csrr  t1,   CSR_SEPC
sd    t1,   OFFSET_REG_SEPC(sp)
csrr  t1,   CSR_STVAL
sd    t1,   OFFSET_REG_SBADADDR(sp)
csrr  t1,   CSR_SCAUSE
sd    t1,   OFFSET_REG_SCAUSE(sp)
```

恢复时对应恢复即可





### init_pcb_stack额外操作

1. 设置csr寄存器，便于中断处理

```c
pt_regs->sepc = entry_point;
pt_regs->sstatus = SR_SPIE & ~SR_SPP;   // ensure not modify other bits!!
```

为了让 `sret` 能正确跳回用户态执行，需要：

1. 清除 `SPP` → 返回用户态（U-mode）
2. 置位 `SPIE` → 允许用户态打开中断（即返回后 SIE = 1）



2. 初始化pcb的user_stack，用于上下文保存和恢复

```c
pcb->user_sp = user_stack;
```





### 睡眠进程的唤醒

**每次进入do_scheduler调度，都会check_sleeping()**，唤醒达到wakeup_time的进程，并放入ready_queue末尾

```c
void do_scheduler(void)
{
//     // TODO: [p2-task3] Check sleep queue to wake up PCBs
    check_sleeping();
    ...
}
void check_sleeping()
{
    list_node_t* cur = sleep_queue.next;
    int len = 0;
    while(cur != &sleep_queue)
    {
        cur = cur->next;
        len++;
    }
    uint64_t cur_time = get_timer();
    cur = sleep_queue.next;
    for(int i=0; i<len; i++)
    {
        queue_popfront(cur);
        if((LIST_TO_PCB(cur))->wakeup_time <= cur_time)
            queue_pushback(&ready_queue, cur);
        else
            queue_pushback(&sleep_queue, cur);
    }
}
```



task3和task4区别就在于**每次进入do_scheduler的时机不同**：

（1）task3：在每次**用户程序内部进行sys_yield**主动让出进程

（2）task4：每次**时钟中断时**，调用handle_irq_timer函数时进行调度

```c
void handle_irq_timer(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p2-task4] clock interrupt handler.
    // Note: use bios_set_timer to reset the timer and remember to reschedule
    set_timer(get_ticks() + TIMER_INTERVAL);    // Supervisor mode
    do_scheduler();
}
```

## Task5

### 时间片相关定义

这里需要我们实现一个复杂调度算法，实现5个飞机程序的同步飞行。在此处由于需要分配时间片，在PCB结构体的定义中我添加了几个结构。

```c
int workload;	
int check_point;
int time_slice;				// 分配的时间片
int time_slice_remaining;	// 剩余的时间片
```

同时，在每次时钟中断进行`do_scheduler`调度时，需要模拟时间片消耗-1：

```c
void handle_irq_timer(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p2-task4] clock interrupt handler.
    // Note: use bios_set_timer to reset the timer and remember to reschedule
    set_timer(get_ticks() + TIMER_INTERVAL);    // Supervisor mode
    if(current_running != NULL && current_running->status == TASK_RUNNING)
    {
        if(current_running->time_slice_remaining > 0)
            current_running->time_slice_remaining--;
    }
    do_scheduler();
}
```



### 调度算法

在每次进入`do_scheduler`时，需要进行时间片的分配更新。这里我采用一个虚拟进度方法：**从起点到check_point为50%进度，从check_point到终点为50%进度**。

每次更新时：

1. 计算5个进程的平均进度`avg_progress`

2. 对每个进程计算当前进度与平均进度的差值，**对于差值<0（落后于平均进度）的，给予时间片的激励**

3. 对于超前的进程给予惩罚，不过需要保证不小于自定义的 `T_MIN`

   

这里需要注意一点，怎么保证5个飞机同时到达终点又同时回到起点出发呢？

对此分配时间片时需要先遍历所有进程，如果发现有的进程在起点而有的进程在终点，则**将在终点的进程阻塞**（分配时间片为0）

```c
...
else if (start_line_barrier_active && vp == 0)
{
    tasks[i]->time_slice = tasks[i]->time_slice_remaining = 0;
    continue;
}
```