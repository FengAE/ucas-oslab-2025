#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/time.h>
#include <os/mm.h>
#include <screen.h>
#include <printk.h>
#include <assert.h>
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

void do_scheduler(void)
{
//     // TODO: [p2-task3] Check sleep queue to wake up PCBs
    check_sleeping();
//     /************************************************************/
//     /* Do not touch this comment. Reserved for future projects. */
//     /************************************************************/

//     // TODO: [p2-task1] Modify the current_running pointer.

    // Update scheduler slices based on current workloads
    if(current_running->pid != 0)
        update_scheduler_slices();

    if(current_running == NULL) return;
    if(current_running->status == TASK_READY)
    {
        current_running->status = TASK_RUNNING;
    }
    else if(current_running->status == TASK_RUNNING)
    {
        if(current_running->pid == 0 || current_running->time_slice_remaining<=0)
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

// ---------------- [p2-task5] -----------------
void set_sched_workload(int workload)
{
    if(current_running == NULL || current_running->pid == 0)
        return;
    current_running->workload = workload;
}

void update_scheduler_slices(void)
{
    pcb_t* tasks[NUM_MAX_TASK];   
    int v_progress[NUM_MAX_TASK]; // virtual progress
    int task_count = 0;
    list_node_t* cur = ready_queue.next;
    while (cur->next != ready_queue.next) 
    {
        pcb_t* p = LIST_TO_PCB(cur);
        if (p->pid != 0)
        {
            tasks[task_count] = p;
            v_progress[task_count] = get_virtual_progress(p); 
            task_count++;
        }
        cur = cur->next; 
    }
    if (current_running != NULL && current_running->pid != 0)
    {
        tasks[task_count] = current_running;
        v_progress[task_count] = get_virtual_progress(current_running); 
        task_count++;
    }
    if (task_count == 0) return;

    int non_finished_count = 0; 
    long sum_progress = 0;
    
    bool some_at_start = false;     
    bool some_at_finish = false;

    for (int i = 0; i < task_count; i++)
    {
        if (v_progress[i] < 990)
        {
            non_finished_count++;
        }
        if (v_progress[i] == 0)     // at start point
            some_at_start = true;
        if (v_progress[i] >= 990)   // at end point
            some_at_finish = true;

        sum_progress += v_progress[i];
    }

    // some at start and some at end: wait
    bool start_line_barrier_active = (non_finished_count > 0) && some_at_start && some_at_finish;

    if (non_finished_count == 0)    // all finished
    {
        for (int i = 0; i < task_count; i++)
            tasks[i]->time_slice = tasks[i]->time_slice_remaining = T_MIN; 
    }
    else
    {
        long avg_progress = sum_progress / task_count;

        long sum_weights = 0; 
        int weights[NUM_MAX_TASK];
        for (int i = 0; i < task_count; i++)
        {
            long delta = avg_progress - v_progress[i]; 
            weights[i] = (delta > 0) ? (int)delta : 0;
            sum_weights += weights[i];
        }

        // distribute time slices
        for (int i = 0; i < task_count; i++)
        {
            int vp = v_progress[i];
            int time_slice;
            if (vp >= 990)  // at end: wait
            {
                tasks[i]->time_slice = tasks[i]->time_slice_remaining = 0;
                continue;
            }
            else if (start_line_barrier_active && vp == 0)
            {
                tasks[i]->time_slice = tasks[i]->time_slice_remaining = 0;
                continue;
            }
            else
            {
                if (sum_weights > 0)
                {
                    time_slice = (int)(((long long)weights[i] * TOTAL_T + (sum_weights / 2)) / sum_weights);
                }
                else
                {
                    time_slice = T_MIN;
                }

                int dynamic_T_min = (vp > avg_progress) ? 1 : T_MIN;
                if (time_slice < dynamic_T_min)
                    time_slice = dynamic_T_min;
            }

            if (time_slice < T_MIN) time_slice = T_MIN;
            if (time_slice > TOTAL_T) time_slice = TOTAL_T;

            tasks[i]->time_slice = time_slice;
            if(tasks[i]->time_slice_remaining > time_slice)
                tasks[i]->time_slice_remaining = time_slice;
        }
    }
}

int get_virtual_progress(pcb_t* p)
{
    int cur_pos = LENGTH - p->workload;
    int cp = p->check_point;
    if (cur_pos <= 0) return 0;
    if (cur_pos >= LENGTH) return 1000;

    int v_progress;
    if (cur_pos < cp)
    {
        // State 1: progress = (cur_pos / cp) * 500
        v_progress = (int)((long long)cur_pos * 500 / cp);
    }
    else
    {
        // State 2: progress = 500 + [(cur_pos-cp)/(LENGTH-cp)]*500
        int phase2_numerator = (cur_pos - cp) * 500;
        int phase2_denominator = LENGTH - cp;
        // avoid (LENGTH - cp) to be 0
        if (phase2_denominator <= 0) phase2_denominator = 1;
        v_progress = 500 + (phase2_numerator / phase2_denominator);
    }

    if (v_progress > 1000) v_progress = 1000;
    return v_progress;
}