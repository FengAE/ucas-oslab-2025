#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/time.h>
#include <os/mm.h>
#include <screen.h>
#include <printk.h>
#include <assert.h>
#define LIST_TO_PCB(node) (pcb_t*)((char*)node- 16)

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
list_node_t* cur_ready = &ready_queue;   // ready_queue cur ptr 

void do_scheduler(void)
{
    // TODO: [p2-task3] Check sleep queue to wake up PCBs

    /************************************************************/
    /* Do not touch this comment. Reserved for future projects. */
    /************************************************************/

    // TODO: [p2-task1] Modify the current_running pointer.
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
        current_running = LIST_TO_PCB(cur_ready);
        current_running->status = TASK_RUNNING;
        switch_to(prev_running->kernel_sp, current_running->kernel_sp);
    }
}

void do_sleep(uint32_t sleep_time)
{
    // TODO: [p2-task3] sleep(seconds)
    // NOTE: you can assume: 1 second = 1 `timebase` ticks
    // 1. block the current_running
    // 2. set the wake up time for the blocked task
    // 3. reschedule because the current_running is blocked.
}

void do_block(list_node_t *pcb_node, list_head *queue)
{
    // TODO: [p2-task2] block the pcb task into the block queue
    queue_pushback(queue, pcb_node);       // push to block_queue
    current_running->status = TASK_BLOCKED;
    do_scheduler();  
}

void do_unblock(list_node_t *pcb_node)
{
    // TODO: [p2-task2] unblock the `pcb` from the block queue
    queue_remove(pcb_node); // remove from block_queue
    pcb_t* cur_pcb = LIST_TO_PCB(pcb_node);
    cur_pcb->status = TASK_READY;
    queue_pushback(&ready_queue, pcb_node);
}

bool queue_empty(list_node_t* queue)
{
    return queue->next == queue;
}

void queue_pushback(list_head* queue, list_node_t* node)
{
    list_node_t* tail = queue->prev;
    tail->next = node, node->next = queue;
    queue->prev = node, node->prev = tail;
}

void queue_remove(list_node_t* node)    
{   
    if(queue_empty(node))   return; // not in any queue
    list_node_t* prev = node->prev;
    prev->next = node->next;
    node->next->prev = prev;
}

void move_next(list_head* queue)
{
    cur_ready = cur_ready->next;
    if(cur_ready == queue)  // last one is the tail
        cur_ready = queue->next;
}