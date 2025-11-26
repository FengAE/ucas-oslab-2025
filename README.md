## Task1 shell指令实现

### Exec

开始执行某个进程

```c
pid_t sys_exec(char *name, int argc, char **argv) {
    // 1. 查找空闲的 PCB
    pcb_t *pcb = find_free_pcb();	// 遍历
    if (!pcb) return 0;

    // 2. 初始化 PCB 基本信息
    pcb->pid = ...;
    pcb.entry = load_task_img(name, pcb);
    pcb->status = TASK_READY;
    init_list_head(&pcb->wait_list); // 初始化等待队列    

    // 3. 初始化栈和上下文
    // A/C-Core: 将 argc 和 argv 拷贝到用户栈，并设置 a0, a1 [cite: 562, 566]
    init_pcb_stack(pcb, argc, argv);

    // 4. 加入就绪队列
    queue_pushback(&ready_queue, &pcb->list);
    return pcb->pid;
}
```



### Kill

强制结束指定 pid 的进程

```c
int sys_kill(pid_t pid) {
    pcb_t *target = find_pcb_by_pid(pid);
    if (!target) return 0;

    // 1. 资源清理
    // 遍历所有锁，如果持有者是该 pid，则释放锁 
    release_locks_held_by(pid);

    // 2. 唤醒等待该进程的任务 (处理 waitpid)
    while (!list_empty(&target->wait_list)) {
        unblock_task(target->wait_list.next);
    }

    // 3. 修改状态并调度
    // 如果进程在就绪队列或睡眠队列，将其移除
    remove_from_queues(target);
    target->status = TASK_EXITED;
    // 如果杀掉的是当前进程，需要调度
    if (target == current_running) {
        do_scheduler();
    }
    return 1;
}
```

必须检查它是否持有内核资源（如互斥锁）。如果不处理，等待该锁的其他进程将陷入死锁。



### Waitpid

当前进程**阻塞等待**某个进程结束

```c
int sys_waitpid(pid_t pid) {
    pcb_t *target = find_pcb_by_pid(pid);
    // 1. 如果进程不存在或已经退出，直接返回
    if (!target || target->status == TASK_EXITED) {
        return pid;
    }
    // 2. 阻塞当前进程
    current_running->status = TASK_BLOCKED;
    
    // 3. 将当前进程加入目标进程的等待队列 
    queue_pushback(&target->wait_list, &current_running->list);
    do_scheduler();
    return pid;
}
```



### Exit

```c
void sys_exit(void) {
    // 1. 资源回收, 释放持有的锁 
    release_locks_held_by(current_running->pid);

    // 2. 唤醒等待者
    // 将 wait_list 中的所有进程状态设为 READY 并加入就绪队列 
    while (!list_empty(&current_running->wait_list)) {
        unblock_task(current_running->wait_list.next);
    }
    current_running->status = TASK_EXITED;
    do_scheduler();
}
```



## Task2 同步原语实现

### 条件变量与屏障

1、屏障

维护一个计数器 `count` 和目标值 `goal`。每当进程到达屏障调用 `wait`，计数器加 1 并阻塞该进程。当 `count == goal` 时，唤醒所有阻塞的进程 。

```c
struct barrier {
    int goal;      // 目标到达数量
    int count;     // 当前到达数量
    list_head wait_queue; // 等待队列
};

void barrier_wait(int barrier_id) 
{
    struct barrier *b = get_barrier(barrier_id);
    b->count++;
    if (b->count >= b->goal) {
        // 最后一个到达，唤醒所有人
        b->count = 0; 
        broadcast(b->wait_queue)
    } else {
        // 未全部到达，阻塞当前进程
        current_running->status = TASK_BLOCKED;
        queue_pushback(&b->wait_queue, &current_running->list);
        do_scheduler();
    }
}
```

2、条件变量

```c
list_head cond_queue;
void cond_wait(int cond_id, int mutex_id) {
    mutex_unlock(mutex_id);
    
    // 阻塞当前进程并加入条件变量等待队列
    current_running->status = TASK_BLOCKED;
    list_add(&cond_queue[cond_id], &current_running->list);
    do_scheduler();

    mutex_lock(mutex_id);
}

void cond_signal(int cond_id) {
    if (!list_empty(&cond_queue[cond_id])) {
        unblock_task(cond_queue[cond_id].next);
    }
}

void cond_broadcast(int cond_id) {
    // 唤醒队列中所有进程 [cite: 199]
    while (!list_empty(&cond_queue[cond_id])) {
        unblock_task(cond_queue[cond_id].next);
    }
}
```

如果发生中断？	加锁保护

多核访问：

1. **大内核锁**策略：进入内核即加锁。即使发生中断触发调度，其他核心也无法获取该锁，从而保证数据安全 	
2. **细粒度锁**，通常在获取自旋锁时需要**关闭本地中断**，防止当前核在持有锁时被中断打断并调度到另一个试图获取同一把锁的进程（导致死锁）。







### Mailbox

Mailbox 是一个有界缓冲区（Bounded Buffer），遵循 FIFO 原则。包含发送者（Producer）和接收者（Consumer）。

**Send**：发送方将消息存入信箱。如果信箱满（空间不足），发送方阻塞 。

**Recv**：接收方从信箱取出消息。如果信箱空（数据不足），接收方阻塞 。

```
           STR_MBOX                         POS_MBOX
   ┌────────────────────┐           ┌─────────────────────┐
   │  Client            │           │        Server       │
   │  send MsgHeader    │  ---->    │  recv MsgHeader     │
   │  send Content      │           │  recv Content       │
   │                    │           │                     │
   │  recv position <---│-----------│  send position      │
   └────────────────────┘           └─────────────────────┘
```

1、数据互斥

自旋锁（Spinlock）保护 Mailbox 结构体。在进行 `sys_mbox_send` 或 `sys_mbox_recv` 的数据拷贝操作前加锁，操作后释放锁

2、同步机制（生产者消费者模型）

条件变量使用









## Task3 双核启动

### boot启动

#### **bootblock.S**

当上电后，Core 0 和 Core 1 同时启动，都运行 `bootblock.S`。

Core 1需要一直等待核间中断，当中断来临时进入stvel设置的kernel，开始设置Core 1的环境

开头需要关中断，因为硬件复位状态不确定，此时stvec还没有设置好，如果不关中断，可能会跑到一个不符合预期的地址

```asm
main:
	fence
    // a0 is mhartid
	bnez a0, secondary
	# cpu0: set bois fun
	# ......
# cpu1:
secondary:
	# disable all interrupts
	li 		t0, SR_SIE
	csrc	sstatus, t0	
	# set stvec
	la		t0, kernel
	csrw	stvec, t0
	# enable software interrupts
	li		t0, SIE_SSIE
	csrs	sie, t0
	li 		t0, SR_SIE
	csrs 	sstatus, t0
wait_for_wakeup:
	wfi
	j wait_for_wakeup
```



#### **head.S**

```asm
#define KERNEL_STACK		0x50500000
#define KERNEL_STACK_2  0x50501000
.section ".entry_function","ax"
ENTRY(_start)
  # ......

clear_bss:
	# ......
end_clear_bss:
	# ......
secondary_start:
  /* Slave Core Stack Setup */
  /* Use a different stack area to avoid conflict */
  la    sp, KERNEL_STACK_2
  call  main

loop:
  wfi
  j loop

END(_start)
```

Core 0：初始化bss段

Core 1：由于c环境已经初始化，只需要跳到main即可。注意这里的内核栈需要另外设置防止冲突



#### **main**

```c
int main(void)
{
    int id = get_current_cpu_id();
    if(id == 0)	// Core 0
    {
        init_locks();
        smp_init();
        lock_kernel();
        // ....
        unlock_kernel();
        wakeup_other_hart(NULL);
    }
    else
    {
        lock_kernel();
        setup_exception();
        unlock_kernel();
    }
}
```

在Core 0已经实现task、PCB、锁的初始化，故而在唤醒Core 1时，只需要设置中断初始化即可

使用`send_ipi`进行软件中断后，**会跳转到Core 1设置的stvel，即kernel处**，进行Core 1的内核初始化

注意：Core 1在unlock后，需要清除SIE位，因为此时SIE依然是1，操作系统会认为还有一个pending的软件中断没有处理，会在enable_preempt后，直接进入软件中断处理，而非处理时钟中断进行调度



### 从核中断

大内核锁保证，同一时间只有一个核能执行内核代码。所以在进入内核态(trap_entry)时上锁，离开时解锁

```asm
ENTRY(ret_from_exception)
  call  unlock_kernel
  RESTORE_CONTEXT
  sret
ENDPROC(ret_from_exception)

ENTRY(exception_handler_entry)
  SAVE_CONTEXT

  call  lock_kernel
  
  mv    a0, sp        
  csrr  a1, CSR_STVAL
  csrr  a2, CSR_SCAUSE    
  call  interrupt_helper
  j     ret_from_exception

ENDPROC(exception_handler_entry)
```

这里，由于`lock_kernel`作为一个函数会修改寄存器，故而**需要在`SAVE_CONTEXT`后进行调用**，否则保存的上下文就是被修改过的数据



### 独立变量

对于双核系统来说，`current_running` 和 `pid0_pcb` 均是各自独立的，所以要将原来单变量的改为数组。

```c
extern pcb_t pid0_pcb[NR_CPUS];
extern ptr_t pid0_stack[NR_CPUS];
extern pcb_t* current_running[NR_CPUS];
```

原来每次对`current_running` 的操作，只需要先通过 `get_current_cpu_id`()  获取`cpu_id`，再操作对应的 `current_running[cpu_id]` 即可。





## Task4 绑核操作

这里需要实现一个taskset指令，将进程绑定在某个核

为此，需要在pcb中添加mask和cpu_id成员

```c
int mask;   // which cpu allow to run
int cpu_id;	// running on which cpu
```

1、对于shell指令，只需要能解析特定taskset指令即可。当执行taskset指令时，直接更新对应的mask

2、修改调度逻辑，从尾到头遍历，删除exited的进程，略过不满足mask的进程

```c
// From tail to head, search: not exit and satisfy mask
while (cur != &ready_queue) 
{
    pcb_t* candidate = LIST_TO_PCB(cur);
    list_node_t* prev_node = cur->prev;
    if(candidate->status == TASK_EXITED)
    {
        // remove from ready_queue
        prev_node->next = cur->next;
        cur->next->prev = prev_node;
    }
    else if(candidate->mask & (1<<cpu_id))  // satisfy mask
    {
        prev_node->next = cur->next;
        cur->next->prev = prev_node;
        next_pcb = candidate;
        break;
    }
    cur = prev_node;
}
```





## Task5 死锁避免

通过在读mbox1、写mbox2时，创建两个进程分别进行读写来避免死锁。

由于目前还没有实现页表，线程的实现和进程基本没什么区别，只是注意：

1. 创建线程的 entry 需要手动传入，而非在task里面寻找
2. 线程传参也可以自定义

```c
void do_thread_create(int* thread_id, void *target, void* arg)
{
    int i;
    for (i = 0; i < NUM_MAX_TASK; i++) 
    {
        if (pcb[i].status == TASK_EXITED) break;
    }
    if (i == NUM_MAX_TASK) 
    {
        printk("thread create failed, no free\n");
        *thread_id = -1;
        return;
    }
    if(!target) // want to jump to 0x0
    {
        printk("Error: pthread entry can't be 0x0\n");
        return;
    }

    int cpu_id = get_current_cpu_id();
    pcb_t *parent = current_running[cpu_id];
    pcb_t *thread = &pcb[i];

    // Initialize based on parent
    thread->pid = ++pcb_num;
    thread->status = TASK_READY;
    thread->mask = parent->mask; // Share affinity
    thread->cpu_id = parent->cpu_id;
    strcpy(thread->name, parent->name); 

    // Allocate NEW stacks (Threads share address space but need unique stacks)
    thread->user_stack_base = allocUserPage(1) + PAGE_SIZE;
    thread->kernel_stack_base = allocKernelPage(1) + PAGE_SIZE;
    init_pcb_stack(thread->kernel_stack_base, thread->user_stack_base,
                   (ptr_t)target, thread);
    
    // Pass argument in a0
    regs_context_t *pt_regs = (regs_context_t *)(thread->kernel_stack_base - sizeof(regs_context_t));
    pt_regs->regs[10] = (reg_t)arg; 

    thread->wakeup_time = 0;
    thread->time_slice = parent->time_slice;
    thread->time_slice_remaining = thread->time_slice;
    thread->workload = parent->workload;
    thread->lock_ptr = 0;
    init_list_head(&thread->wait_list);
    queue_pushfront(&thread->list, &ready_queue);

    *thread_id= thread->pid;
}
```

