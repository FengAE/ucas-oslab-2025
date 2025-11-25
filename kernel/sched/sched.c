#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/time.h>
#include <os/task.h>
#include <os/loader.h>
#include <os/string.h>
#include <os/mm.h>
#include <screen.h>
#include <printk.h>
#include <assert.h>

pcb_t pcb[NUM_MAX_TASK]; 
ptr_t pid0_stack[NR_CPUS];
pcb_t pid0_pcb[NR_CPUS];
pcb_t* current_running[NR_CPUS];

// pcb_t pid0_pcb = {
//     .pid = 0,
//     .kernel_sp = (ptr_t)pid0_stack,
//     .user_sp = (ptr_t)pid0_stack,
//     .status = TASK_RUNNING,
//     .name = "IDLE"
// };


// OFFSET: based on sched.h
#define OFFSETOF_LIST 16
#define LIST_TO_PCB(node) (pcb_t*)((char*)node - OFFSETOF_LIST)

LIST_HEAD(ready_queue);
LIST_HEAD(sleep_queue);

static const char* status_str[] = {
    [TASK_BLOCKED] = "BLOCKED",
    [TASK_RUNNING] = "RUNNING",
    [TASK_READY]   = "READY",
    [TASK_EXITED]  = "EXITED"
};

void do_scheduler(void)
{
    //     // TODO: [p2-task3] Check sleep queue to wake up PCBs
    int cpu_id = get_current_cpu_id();
    check_sleeping();
//     /************************************************************/
//     /* Do not touch this comment. Reserved for future projects. */
//     /************************************************************/

//     // TODO: [p2-task1] Modify the current_running pointer.
    pcb_t* next_pcb = NULL;
    list_node_t* cur = ready_queue.prev;

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

    // no runable task
    if (next_pcb == NULL) 
        next_pcb = &pid0_pcb[cpu_id];

    if(next_pcb->status != TASK_EXITED)
        next_pcb->status = TASK_RUNNING;

    pcb_t* prev_running = current_running[cpu_id];
    if(prev_running->status == TASK_RUNNING && prev_running->pid != 0)
        queue_pushfront(&(prev_running->list), &ready_queue);
    if(prev_running->status != TASK_EXITED)
        prev_running->status = TASK_READY;

    next_pcb->cpu_id = cpu_id;
    current_running[cpu_id] = next_pcb;

    // Have to switch_to, even prev or cur is exited!!
    // else: current_running changed, but stack not changed --> load fault
    if (prev_running != next_pcb) 
        switch_to(prev_running, next_pcb);
}

void queue_pushfront(list_node_t* t, list_head* queue){
    list_node_t* origfst = queue->next;
    queue->next = t;
    t->prev = queue;
    t->next = origfst;
    origfst->prev = t;
}

list_node_t* queue_popback(list_head* queue){
    list_node_t* rear = queue->prev;
    if(rear == queue){
        return NULL;
    }
    rear->prev->next = rear->next;
    rear->next->prev = rear->prev;
    return rear;
}

void check_sleeping()
{
    list_node_t* cur = sleep_queue.next;
    list_node_t* next;
    uint64_t cur_time = get_timer();

    while (cur != &sleep_queue) 
    {
        next = cur->next;
        pcb_t *task = LIST_TO_PCB(cur);
        if (task->wakeup_time <= cur_time) 
        {   // remove by hand
            cur->prev->next = cur->next;
            cur->next->prev = cur->prev;
            
            task->status = TASK_READY;
            queue_pushfront(cur, &ready_queue);
        }
        cur = next;
    }
}

void do_sleep(uint32_t sleep_time)
{
    if(ready_queue.next == &ready_queue) return;
    int cpu_id = get_current_cpu_id();

    pcb_t* next_pcb = LIST_TO_PCB(queue_popback(&ready_queue));
    pcb_t* prev_running = current_running[cpu_id];
    uint64_t current_time = get_timer();

    prev_running->wakeup_time = current_time + (uint64_t)sleep_time;
    queue_pushfront(&(current_running[cpu_id]->list), &sleep_queue);
    current_running[cpu_id]->status = TASK_BLOCKED;
    current_running[cpu_id] = next_pcb;
    next_pcb->status = TASK_RUNNING;
    switch_to(prev_running, next_pcb);
}

void do_block(list_node_t *pcb_node, list_head *queue)
{
    if(ready_queue.next == &ready_queue) return;

    int cpu_id = get_current_cpu_id();
    pcb_t* next_pcb = LIST_TO_PCB(queue_popback(&ready_queue));
    next_pcb->status = TASK_RUNNING;

    queue_pushfront(pcb_node, queue);
    current_running[cpu_id]->status = TASK_BLOCKED; 
    pcb_t* prev_running = current_running[cpu_id];
    current_running[cpu_id] = next_pcb;
    switch_to(prev_running, next_pcb);
}

int do_unblock(list_node_t *node)
{
    list_node_t* cur = queue_popback(node);
    if(!cur) return 0;
    pcb_t* prev_running = LIST_TO_PCB(cur);
    prev_running->status = TASK_READY;
    queue_pushfront(cur, &ready_queue);
    return 1;
}

void do_process_show(int startline)
{
    int idx = 0;
    for(int i=0; i<pcb_num; i++)
    {
        if(pcb[i].status != TASK_EXITED)
        {
            printk("[%d] PID: %d  STATUS: %s  \tmask: 0x%d   \tNAME: %s", idx++, pcb[i].pid, 
                        status_str[pcb[i].status], pcb[i].mask, pcb[i].name);
            if(pcb[i].status == TASK_RUNNING)
                printk("    \tRunning on core %d", pcb[i].cpu_id);
            printk("\n");
        }
    }   
}

int do_waitpid(pid_t pid)
{
    int i;
    for(i = 0; i < NUM_MAX_TASK; i++)   
        if(pid == pcb[i].pid) break;    
    if(i == NUM_MAX_TASK || pcb[i].status == TASK_EXITED) 
        return 0;   // already exited

    int cpu_id = get_current_cpu_id();
    if(current_running[cpu_id]->status == TASK_RUNNING)
        queue_pushfront(&(current_running[cpu_id]->list), &(pcb[i].wait_list));

    if(ready_queue.next == &ready_queue) return 0;
    pcb_t* next_pcb = LIST_TO_PCB(queue_popback(&ready_queue));
    next_pcb->status = TASK_RUNNING;
    
    current_running[cpu_id]->status = TASK_BLOCKED;
    pcb_t* prev_running = current_running[cpu_id];
    current_running[cpu_id] = next_pcb;
    switch_to(prev_running, next_pcb);
    
    return pid;
}

void do_exit()
{
    int cpu_id = get_current_cpu_id();
    current_running[cpu_id]->status = TASK_EXITED;
    
    // release locks
    if (current_running[cpu_id]->lock_id >= 0) 
        do_mutex_lock_release(current_running[cpu_id]->lock_id);
        
    while(current_running[cpu_id]->wait_list.next != &(current_running[cpu_id]->wait_list))
        do_unblock((list_node_t*)&(current_running[cpu_id]->wait_list));
    current_running[cpu_id]->lock_id = -1;
    do_scheduler();
}

int do_kill(pid_t pid)
{
    int cpu_id = get_current_cpu_id();
    if(pid == current_running[cpu_id]->pid) 
    {
        do_exit();    
        return 1;
    }
    int i;
    for(i = 0; i < NUM_MAX_TASK; i++)
        if(pcb[i].pid == pid) break;
    if(i == NUM_MAX_TASK || pcb[i].status == TASK_EXITED) 
        return 0;
    pcb[i].list.next->prev = pcb[i].list.prev;
    pcb[i].list.prev->next = pcb[i].list.next;
    pcb[i].status = TASK_EXITED;
    if(pcb[i].lock_id >= 0)
        do_mutex_lock_release(pcb[i].lock_id);
    while(pcb[i].wait_list.next != &(pcb[i].wait_list))
        do_unblock((list_node_t*)&(pcb[i].wait_list));
        
    pcb[i].lock_id = -1;
    return 1;
}

pid_t do_getpid()
{
    int cpu_id = get_current_cpu_id();
    return current_running[cpu_id]->pid;
}

pid_t do_exec(char *name, int argc, char *argv[])
{
    int i;
    for (i = 0; i < NUM_MAX_TASK; i++) 
    {
        // Here enable multiple instances of the same program
        // if(strcmp(pcb[i].name, name) == 0 && pcb[i].status != TASK_EXITED)
        //     return -1;  // already exists
        if (pcb[i].status == TASK_EXITED) break;
    }
    if (i == NUM_MAX_TASK) return -2;   // no free
    pcb[i].entry = load_task_img(name, tasks, tasknum);
    if(pcb[i].entry == 0) return -3;    // load img failed

    pcb[i].pid = ++pcb_num; 
    pcb[i].status = TASK_READY;
    strcpy(pcb[i].name, name);
    pcb[i].lock_id = -1; // init lock_id

    // set cpu_id and mask
    int cpu_id = get_current_cpu_id();
    pcb[i].cpu_id = cpu_id;
    if(current_running[cpu_id]->pid > 1)    // not launched by shell or pid0
        pcb[i].mask = current_running[cpu_id]->mask;    // the same with parent

    pcb[i].kernel_stack_base = allocKernelPage(1)+PAGE_SIZE;
    pcb[i].user_stack_base = allocUserPage(1)+PAGE_SIZE;
    init_pcb_stack(pcb[i].kernel_stack_base, pcb[i].user_stack_base,
                   pcb[i].entry, &pcb[i]);
    
    pcb[i].wakeup_time = 0;
    pcb[i].time_slice = pcb[i].time_slice_remaining = 1;
    pcb[i].workload = 1;
    init_list_head(&(pcb[i].wait_list));

    ptr_t argv_base = pcb[i].user_stack_base - argc * sizeof(char*);
    ptr_t current_sp = argv_base;
    ptr_t *argv_ptr_array = (ptr_t *)argv_base;

    for(int j = 0; j < argc; j++) {
        int len = strlen(argv[j]) + 1;
        current_sp -= len;
        strcpy((char *)current_sp, argv[j]);
        argv_ptr_array[j] = current_sp; 
    }
    argv_ptr_array[argc] = 0;
    current_sp = current_sp & ~((ptr_t)0xF);

    regs_context_t *pt_regs = (regs_context_t *)(pcb[i].kernel_stack_base - sizeof(regs_context_t));
    pt_regs->regs[2] = current_sp;       // sp
    pt_regs->regs[10] = (reg_t)argc;     // a0
    pt_regs->regs[11] = (reg_t)argv_base;// a1
    pcb[i].user_sp = current_sp; 

    queue_pushfront(&(pcb[i].list), &ready_queue);

    return pcb[i].pid;
}

pid_t do_taskset(void *target, int mask, int mode)
{
    if(!mode)    // only change mask, target is pid
    {
        int pid = (int)target;
        for(int i=0; i<NUM_MAX_TASK; i++)
        {
            if(pcb[i].status != TASK_EXITED && pcb[i].pid == pid)
            {
                pcb[i].mask = mask;
                return pid;
            }
        }
        printk("taskset failed\n");
        return -1;
    }
    else    // need to exec
    {
        char* name = (char*)target;
        char* argv[1] = {name};
        pid_t id = do_exec(name, 1, argv);
        if(id >= 0)
        {
            for(int i=0; i<NUM_MAX_TASK; i++)
            {
                if(pcb[i].pid == id)    pcb[i].mask = mask;
            }
        }
        else    printk("taskset failed\n");
        return id;
    }
}

void set_sched_workload(int workload)
{
    int cpu_id = get_current_cpu_id();
    if(current_running[cpu_id] == NULL || current_running[cpu_id]->pid == 0)
        return;
    current_running[cpu_id]->workload = workload;
}

void init_list_head(list_head* head)
{
    head->next = head->prev = head;
}

// thread func
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
    thread->lock_id = -1;
    init_list_head(&thread->wait_list);
    queue_pushfront(&thread->list, &ready_queue);

    *thread_id= thread->pid;
}

void do_thread_exit(void)
{
    int cpu_id = get_current_cpu_id();
    pcb_t *current = current_running[cpu_id];
    current->status = TASK_EXITED;
    if (current->lock_id >= 0) 
    {
        do_mutex_lock_release(current->lock_id);
        current->lock_id = -1;
    }
    // Wake up joiners
    while (current->wait_list.next != &current->wait_list) 
        do_unblock(current->wait_list.next);
    do_scheduler();
}

void do_thread_join(pid_t pid)
{
    do_waitpid(pid);
}