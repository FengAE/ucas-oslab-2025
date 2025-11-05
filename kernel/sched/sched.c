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
    check_sleeping();
//     /************************************************************/
//     /* Do not touch this comment. Reserved for future projects. */
//     /************************************************************/

//     // TODO: [p2-task1] Modify the current_running pointer.

    // Update scheduler priorities based on current workloads
    if(current_running->pid != 0)
        update_scheduler_priorities();

    if(current_running == NULL) return;
    if(current_running->status == TASK_READY)
    {
        current_running->status = TASK_RUNNING;
    }
    else if(current_running->status == TASK_RUNNING)
    {
        if(current_running->pid == 0 || current_running->time_slice_remaining>=0)
        {
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
        }
    //     if(current_running->pid == 0 || current_running->time_slice_remaining <=0)
    //     {
    //         current_running->status = TASK_READY;
    //         pcb_t* prev_running = current_running;

    //         // Select next task based on priority
    //         list_node_t* next_node = select_next_task();
    //         if(next_node == NULL)   // No ready task, keep running current
    //         {
    //             current_running->status = TASK_RUNNING;
    //             return;
    //         }
    //         queue_popfront(&ready_queue);   // remove current_running
    //         current_running->time_slice_remaining = current_running->time_slice;
    //         if(current_running->pid != 0)
    //             queue_pushback(next_node, &current_running->list);
    //         ready_queue = *next_node;
    //         current_running = LIST_TO_PCB(next_node);
    //         current_running->status = TASK_RUNNING;
    //         switch_to(prev_running->kernel_sp, current_running->kernel_sp);
    //     }
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

// ----------- [p2-task5] --------------
void set_sched_workload(int workload)
{
    if(current_running == NULL || current_running->pid == 0)
        return;
    current_running->workload = workload;
}

/**
 * 简单调度策略：
 * 1. 计算所有任务的workload平均值
 * 2. 根据与平均值的差异分配时间片
 *    workload > 平均值（进度慢）-> 增加时间片
 *    workload < 平均值（进度快）-> 减少时间片
 * 
 * 公式：time_slice = BASE_TIME_SLICE + (workload - avg_workload) * SCALE
 * 限制在 [MIN_TIME_SLICE, MAX_TIME_SLICE] 范围内
 */
#define WORKLOAD_SCALE 1  

void update_scheduler_priorities(void)
{
    list_node_t* cur = ready_queue.next;
    int total_workload = 0;
    int ready_count = 0;

    while(cur->next != ready_queue.next)
    {
        pcb_t* p = LIST_TO_PCB(cur);
        if(p->status == TASK_READY && p->pid != 0)
        {
            total_workload += p->workload;
            ready_count++;
        }
        cur = cur->next;
    }
    if(current_running->pid != 0 && current_running->status == TASK_RUNNING)
    {
        total_workload += current_running->workload;
        ready_count++;
    }
    if(ready_count == 0)
        return;

    int avg_workload = total_workload / ready_count;
    // Distribute time slice
    cur = ready_queue.next;
    while(cur->next != ready_queue.next)    
    {   // Actually judge cur==&ready_queue, due to not use list_node_t** in push and pop
        pcb_t* p = LIST_TO_PCB(cur);
        if(p->status == TASK_READY && p->pid != 0)
        {
            int deviation = p->workload - avg_workload;
            // workload larger --> priority larger
            p->priority = p->workload;
            int time_slice = BASE_TIME_SLICE + deviation * WORKLOAD_SCALE;
            if(time_slice < MIN_TIME_SLICE)
                time_slice = MIN_TIME_SLICE;
            if(time_slice > MAX_TIME_SLICE)
                time_slice = MAX_TIME_SLICE;
            p->time_slice = time_slice;

            if(p->time_slice_remaining > time_slice)
                p->time_slice_remaining = time_slice;
        }
        cur = cur->next;
    }
    
    // refresh current_running
    if(current_running != NULL &&  current_running->pid != 0)
    {
        int deviation = current_running->workload - avg_workload;
        current_running->priority = current_running->workload;
        int time_slice = BASE_TIME_SLICE + deviation * WORKLOAD_SCALE;
        if(time_slice < MIN_TIME_SLICE)
            time_slice = MIN_TIME_SLICE;
        if(time_slice > MAX_TIME_SLICE)
            time_slice = MAX_TIME_SLICE;
        current_running->time_slice = time_slice;
    }
}

/**
 * Select the next task to run based on priority
 * Returns the task with highest priority
 */
list_node_t* select_next_task(void)
{
    if(current_running->pid == 0)
        return ready_queue.next; 
    list_node_t* best_task = NULL;
    int best_priority = -1;
    
    list_node_t* cur = ready_queue.next;
    while(cur->next != ready_queue.next)
    {
        pcb_t* p = LIST_TO_PCB(cur);
        if(p->status == TASK_READY && p->pid != 0)
        {
            if(p->priority > best_priority)
            {
                best_priority = p->priority;
                best_task = cur;
            }
        }
        cur = cur->next;
    }
    return best_task;
}
