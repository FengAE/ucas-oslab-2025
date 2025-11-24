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

// based on sched.h
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

void do_scheduler(void)
{
    check_sleeping();
    // pcb empty
    if(ready_queue.next == &ready_queue) return;
    pcb_t* next_pcb = LIST_TO_PCB(queue_popback(&ready_queue));
    if(next_pcb->status != TASK_EXITED)
        next_pcb->status = TASK_RUNNING;

    if(current_running->status == TASK_RUNNING)
        queue_pushfront(&(current_running->list), &ready_queue);  
    if(current_running->status != TASK_EXITED) 
        current_running->status = TASK_READY;
    pcb_t* prev_running = current_running;
    current_running = next_pcb; 

    if(prev_running != current_running && current_running->status != TASK_EXITED) 
        switch_to(prev_running->kernel_sp, current_running->kernel_sp);

}

void do_sleep(uint32_t sleep_time)
{
    if(ready_queue.next == &ready_queue) return;
    pcb_t* next_pcb = LIST_TO_PCB(queue_popback(&ready_queue));
    pcb_t* prev_running = current_running;
    uint64_t current_time = get_timer();

    prev_running->wakeup_time = current_time + (uint64_t)sleep_time;
    queue_pushfront(&(current_running->list), &sleep_queue);
    current_running->status = TASK_BLOCKED;
    current_running = next_pcb;
    next_pcb->status = TASK_RUNNING;
    switch_to(prev_running->kernel_sp, next_pcb->kernel_sp);
}

void do_block(list_node_t *pcb_node, list_head *queue)
{
    if(ready_queue.next == &ready_queue) return;
    pcb_t* next_pcb = LIST_TO_PCB(queue_popback(&ready_queue));
    next_pcb->status = TASK_RUNNING;

    queue_pushfront(pcb_node, queue);
    current_running->status = TASK_BLOCKED; 
    pcb_t* prev_running = current_running;
    current_running = next_pcb;
    switch_to(prev_running->kernel_sp, next_pcb->kernel_sp);
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
            printk("[%d] PID : %d   STATUS: %s  \tNAME: %s\n", idx++, pcb[i].pid, 
                        status_str[pcb[i].status], pcb[i].name);
    }   
}

int do_waitpid(pid_t pid)
{
    int i;
    for(i = 0; i < NUM_MAX_TASK; i++)   
        if(pid == pcb[i].pid) break;    
    if(i == NUM_MAX_TASK || pcb[i].status == TASK_EXITED) 
        return 0;   // already exited
    if(current_running->status == TASK_RUNNING)
        queue_pushfront(&(current_running->list), &(pcb[i].wait_list));

    if(ready_queue.next == &ready_queue) return 0;
    pcb_t* next_pcb = LIST_TO_PCB(queue_popback(&ready_queue));
    next_pcb->status = TASK_RUNNING;
    
    current_running->status = TASK_BLOCKED;
    pcb_t* prev_running = current_running;
    current_running = next_pcb;
    switch_to(prev_running->kernel_sp, next_pcb->kernel_sp);
    
    return pid;
}

void do_exit()
{
    current_running->status = TASK_EXITED;
    
    // release locks
    if (current_running->lock_id >= 0) 
        do_mutex_lock_release(current_running->lock_id);
        
    while(current_running->wait_list.next != &(current_running->wait_list))
        do_unblock((list_node_t*)&(current_running->wait_list));
    current_running->lock_id = -1;
    do_scheduler();
}

int do_kill(pid_t pid)
{
    if(pid == current_running->pid) 
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
    return current_running->pid;
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

void set_sched_workload(int workload)
{
    if(current_running == NULL || current_running->pid == 0)
        return;
    current_running->workload = workload;
}

void init_list_head(list_head* head)
{
    head->next = head->prev = head;
}
