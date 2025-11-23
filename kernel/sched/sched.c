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
#include <os/string.h>
#define LIST_TO_PCB(node) (pcb_t*)((char*)node- 16)
#define LENGTH 60   // used in fly program

pcb_t pcb[NUM_MAX_TASK];
const ptr_t pid0_stack = INIT_KERNEL_STACK + PAGE_SIZE;
pcb_t pid0_pcb = {
    .pid = 0,
    .kernel_sp = (ptr_t)pid0_stack,
    .user_sp = (ptr_t)pid0_stack
};
LIST_HEAD(ready_queue);
LIST_HEAD(sleep_queue);

/* global process id */
pid_t process_id = 1;
list_node_t* prev_running_node = NULL;

// pcb_status --> status str
static const char* status_str[] = {
    [TASK_BLOCKED] = "BLOCKED",
    [TASK_RUNNING] = "RUNNING",
    [TASK_READY] = "READY",
    [TASK_EXITED] = "EXITED"
};

void do_scheduler(void)
{
//     // TODO: [p2-task3] Check sleep queue to wake up PCBs
    check_sleeping();
//     /************************************************************/
//     /* Do not touch this comment. Reserved for future projects. */
//     /************************************************************/

//     // TODO: [p2-task1] Modify the current_running pointer.

    // Update scheduler slices based on current workloads
    // if(current_running->pid != 0)
    //     update_scheduler_slices();

    if(current_running == NULL) return;
    if(current_running->status == TASK_READY)
    {
        current_running->status = TASK_RUNNING;
    }
    else if(current_running->status == TASK_RUNNING)
    {
        // if(current_running->pid == 0 || current_running->time_slice_remaining<=0)
        // {
            current_running->status = TASK_READY;
            pcb_t* prev_running = current_running;
            list_node_t* new_head = queue_popfront(&ready_queue);   // remove current_running
            current_running->time_slice_remaining = current_running->time_slice;
            if(current_running->pid != 0)
                queue_pushback(new_head, &current_running->list);
            ready_queue = *new_head;
            current_running = LIST_TO_PCB(new_head);
            current_running->status = TASK_RUNNING;
            // switch_to(prev_running, current_running);
            switch_to(prev_running->kernel_sp, current_running->kernel_sp);
        // }
    
    }

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

void do_sleep(uint32_t sleep_time)
{
    // TODO: [p2-task3] sleep(seconds)
    // NOTE: you can assume: 1 second = 1 `timebase` ticks
    // 1. block the current_running
    // 2. set the wake up time for the blocked task
    // 3. reschedule because the current_running is blocked.
    current_running->wakeup_time = sleep_time + get_timer();
    do_block(&current_running->list, &sleep_queue);
}

void do_block(list_node_t *pcb_node, list_head *block_queue)
{
    // TODO: [p2-task2] block the pcb task into the block queue
    // USE: do_block(&current_running->list, ...);
    if((LIST_TO_PCB(pcb_node))->status != TASK_BLOCKED)
    {
        // first in: in ready_queue
        list_node_t* new_head = queue_popfront(&ready_queue);
        ready_queue = *new_head;
        current_running->status = TASK_BLOCKED;
        queue_pushback(block_queue, &current_running->list);
        pcb_t* prev_running = current_running;
        current_running = LIST_TO_PCB(new_head);
        current_running->status = TASK_RUNNING;
        // switch_to(prev_running, current_running);
        switch_to(prev_running->kernel_sp, current_running->kernel_sp);
    }
}

bool do_unblock(list_head *queue)
{
    // TODO: [p2-task2] unblock the `pcb` from the block queue
    list_node_t* tmp = queue->next;
    list_node_t* node = queue_popfront(tmp);
    if(!node) return false;
    pcb_t* cur = LIST_TO_PCB(tmp);
    cur->status = TASK_READY;
    queue_pushback(&ready_queue, tmp);
    return true;
}

void queue_pushback(list_head* queue, list_node_t* node)
{
    if(queue->next == queue->prev)
        queue->next = node;
    list_node_t* tail = queue->prev;
    tail->next = node, node->next = queue;
    queue->prev = node, node->prev = tail;
}

// Remove queue, return new_head
list_node_t* queue_popfront(list_head* queue)
{
    if(queue->next == queue)    return NULL;
    list_node_t* Next = queue->next;
    Next->prev = queue->prev;
    queue->prev->next = Next;
    return Next;
}

void init_list_head(list_head* head)
{
    head->next = head->prev = head;
}

// ---------------- [p2-task5] -----------------
void set_sched_workload(int workload)
{
    if(current_running == NULL || current_running->pid == 0)
        return;
    current_running->workload = workload;
}

// -------=---------- [p3] ----------------------
void do_process_show()
{
    list_node_t* cur = ready_queue.next;
    int idx = 0;
    for(int i=0; i<pcb_num; i++)
    {
        if(pcb[i].status != TASK_EXITED)
            printk("[%d] PID : %d   STATUS: %s  \tNAME: %s\n", idx++, pcb[i].pid, 
                        status_str[pcb[i].status], pcb[i].name);
    }   
}

#define MAX_ARGS 16 
pid_t do_exec(char *name, int argc, char *argv[])
{
    int i;
    for (i = 0; i < NUM_MAX_TASK; i++) 
    {
        if(strcmp(pcb[i].name, name) == 0 && pcb[i].status != TASK_EXITED)
            return -1;   // already exists
        if (pcb[i].status == TASK_EXITED) break;
    }
    if (i == NUM_MAX_TASK)  // no free pcb
        return -2;

    pcb[i].entry = load_task_img(name, tasks, tasknum);
    if(pcb[i].entry == 0)
        return -3;   // load failed
    pcb[i].pid = ++pcb_num; 
    pcb[i].status = TASK_READY;
    strcpy(pcb[i].name, name);
    pcb[i].lock_id = -1;
    pcb[i].kernel_stack_base = allocKernelPage(1)+PAGE_SIZE;
    pcb[i].user_stack_base = allocUserPage(1)+PAGE_SIZE;
    
    init_pcb_stack(pcb[i].kernel_stack_base, pcb[i].user_stack_base,
                   pcb[i].entry, &pcb[i]);
    
    pcb[i].wakeup_time = 0;
    pcb[i].time_slice = pcb[i].time_slice_remaining = 1;
    pcb[i].workload = 1;
    pcb[i].check_point = 10; 
    init_list_head(&pcb[i].wait_list);

    ptr_t argv_base = pcb[i].user_stack_base - argc * sizeof(char*);
    ptr_t current_sp = argv_base;
    ptr_t *argv_ptr_array = (ptr_t *)argv_base;

    for(int j = 0; j < argc; j++)
    {
        int len = strlen(argv[j]) + 1; // include \0
        current_sp -= len;
        strcpy((char *)current_sp, argv[j]);
        argv_ptr_array[j] = current_sp; 
    }
    argv_ptr_array[argc] = 0;
    current_sp = current_sp & ~((ptr_t)0xF);

    regs_context_t *pt_regs =
        (regs_context_t *)(pcb[i].kernel_stack_base - sizeof(regs_context_t));

    pt_regs->regs[2] = current_sp;       // sp
    pt_regs->regs[10] = (reg_t)argc;     // a0
    pt_regs->regs[11] = (reg_t)argv_base;// a1
    pcb[i].user_sp = current_sp; 

    queue_pushback(&ready_queue, &(pcb[i].list));

    return pcb[i].pid;
}

int do_waitpid(pid_t pid)
{
    int i;
    for(i=0; i<NUM_MAX_TASK; i++)
    {
        if(pcb[i].pid == pid)   break;
    }
    if (i == NUM_MAX_TASK || pcb[i].status == TASK_EXITED) 
        return 0;   // failed
    do_block(&current_running->list, &pcb[i].wait_list);
    return pid;
}

void do_exit(void)
{
    while(do_unblock(&current_running->wait_list));
    current_running->status = TASK_EXITED;
    list_node_t* new_head = queue_popfront(&ready_queue);   
    ready_queue = *new_head;
    
    // Release lock held by current_running
    if(current_running->lock_id != -1)
        do_mutex_lock_release(current_running->lock_id);
    current_running = LIST_TO_PCB(new_head);
    do_scheduler();
}

int do_kill(pid_t pid)
{
    if(current_running->pid == pid) // If kill current --> exit
        do_exit();

    int i=0;
    for(; i<NUM_MAX_TASK; i++)
        if(pcb[i].pid == pid)   break;
    if(i == NUM_MAX_TASK || pcb[i].status == TASK_EXITED)   return 0;   
    
    list_node_t* new_head = queue_popfront(&pcb[i].list);
    ready_queue = *new_head;
    pcb[i].status = TASK_EXITED;
    if(pcb[i].pid == (LIST_TO_PCB(&ready_queue))->pid)

    if(pcb[i].lock_id != -1)
        do_mutex_lock_release(pcb[i].lock_id);
    return 1;
}

pid_t do_getpid()
{
    return current_running->pid;
}
