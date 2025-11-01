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
list_node_t* prev_running_node = NULL;

void do_scheduler(void)
{
//     // TODO: [p2-task3] Check sleep queue to wake up PCBs
    // check_sleeping();
//     /************************************************************/
//     /* Do not touch this comment. Reserved for future projects. */
//     /************************************************************/

//     // TODO: [p2-task1] Modify the current_running pointer.
    if(current_running == NULL) return;
    if(current_running->status == TASK_READY)
    {
        current_running->status = TASK_RUNNING;
    }
    else if(current_running->status == TASK_RUNNING)
    {
        current_running->status = TASK_READY;
        pcb_t* prev_running = current_running;
        list_node_t* new_head = queue_popfront(&ready_queue);   // remove current_running
        if(current_running->pid != 0)
            queue_pushback(new_head, &current_running->list);
        ready_queue = *new_head;
        current_running = LIST_TO_PCB(new_head);
        current_running->status = TASK_RUNNING;
        // switch_to(prev_running, current_running);
        switch_to(prev_running->kernel_sp, current_running->kernel_sp);
    }

}

void check_sleeping()
{
    list_node_t* cur = sleep_queue.next;
    uint64_t cur_time = get_timer();
    list_node_t* pivort = cur;
    do
    {
        queue_popfront(cur);
        if((LIST_TO_PCB(cur))->wakeup_time <= cur_time)
            queue_pushback(&ready_queue, cur);
        else
            queue_pushback(&sleep_queue, cur);
        cur = sleep_queue.next;
    }while(cur != pivort);
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
    (LIST_TO_PCB(tmp))->status = TASK_READY;
    queue_pushback(&ready_queue, tmp);
    return true;
}

void queue_pushback(list_head* queue, list_node_t* node)
{
    list_node_t* tail = queue->prev;
    tail->next = node, node->next = queue;
    queue->prev = node, node->prev = tail;
}

// return new_head
list_node_t* queue_popfront(list_head* queue)
{
    if(queue->next == queue)    return NULL;
    list_node_t* Next = queue->next;
    Next->prev = queue->prev;
    queue->prev->next = Next;
    return Next;
}