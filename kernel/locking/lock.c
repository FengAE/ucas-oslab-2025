#include <os/lock.h>
#include <os/sched.h>
#include <os/list.h>
#include <atomic.h>
#include <os/string.h>
#include <printk.h>

mutex_lock_t mlocks[LOCK_NUM];
barrier_t barriers[BARRIER_NUM];
condition_t conditions[CONDITION_NUM];

void init_locks(void)
{
    /* TODO: [p2-task2] initialize mlocks */
    for(int i = 0; i < LOCK_NUM; i++)
    {
        mlocks[i].lock.status = UNLOCKED;
        init_list_head(&mlocks[i].block_queue);
        mlocks[i].key = 0;
    }
}

void spin_lock_init(spin_lock_t *lock)
{
    /* TODO: [p2-task2] initialize spin lock */
    atomic_swap(UNLOCKED, (ptr_t)&lock->status);
}

int spin_lock_try_acquire(spin_lock_t *lock)
{
    /* TODO: [p2-task2] try to acquire spin lock */
    return atomic_swap(LOCKED, (ptr_t)&lock->status) == UNLOCKED;
}

void spin_lock_acquire(spin_lock_t *lock)
{
    /* TODO: [p2-task2] acquire spin lock */
    while (atomic_swap(LOCKED, (ptr_t)&lock->status) == LOCKED);
}

void spin_lock_release(spin_lock_t *lock)
{
    /* TODO: [p2-task2] release spin lock */
    atomic_swap(UNLOCKED, (ptr_t)&lock->status);
}

int do_mutex_lock_init(int key)
{
    /* TODO: [p2-task2] initialize mutex lock */
    for(int i=0; i<LOCK_NUM; i++)
    {
        if(mlocks[i].key == key)    return i; // already exists
    }
    for (int i=0; i<LOCK_NUM; i++) 
    {
        if (mlocks[i].key == 0) 
        {  // free lock
            mlocks[i].lock.status = UNLOCKED;
            init_list_head(&mlocks[i].block_queue);
            mlocks[i].key = key;  
            return i;  
        }
    }
    return -1;  // no lock free
}

void do_mutex_lock_acquire(int mlock_idx)
{
    /* TODO: [p2-task2] acquire mutex lock */
    if(mlock_idx < 0)   return;
    int cpu_id = get_current_cpu_id();
    pcb_t* pcb = current_running[cpu_id];
    if(mlocks[mlock_idx].lock.status == UNLOCKED)
    {
        pcb->lock_id[pcb->lock_ptr++] = mlock_idx;
        mlocks[mlock_idx].lock.status = LOCKED;
    }
    else
    {
        do_block(&current_running[cpu_id]->list, &mlocks[mlock_idx].block_queue);
        pcb->lock_id[pcb->lock_ptr++] = mlock_idx;  // when unblocked, get lock
    }
}

void do_mutex_lock_release(int mlock_idx)
{
    /* TODO: [p2-task2] release mutex lock */
    if(!do_unblock(&(mlocks[mlock_idx].block_queue))) 
    {
        mlocks[mlock_idx].lock.status = UNLOCKED;
    }
    // remove it from lock_id!
    int cpu_id = get_current_cpu_id();
    pcb_t* pcb = current_running[cpu_id];
    for (int i = 0; i < pcb->lock_ptr; i++) 
    {
        if (pcb->lock_id[i] == mlock_idx) 
        {
            for (int j = i; j < pcb->lock_ptr - 1; j++) 
            {
                pcb->lock_id[j] = pcb->lock_id[j+1];
            }
            pcb->lock_ptr--;
            break;
        }
    }
}

// ================== barriers ================
void init_barriers(void)
{
    for (int i = 0; i < BARRIER_NUM; i++) 
    {
        barriers[i].key = 0;
        barriers[i].n = 0;
        barriers[i].current_waiting = 0;
        init_list_head(&barriers[i].wait_queue);
    }
}

int do_barrier_init(int key, int n)
{
    for (int i = 0; i < BARRIER_NUM; i++) 
    {
        if (barriers[i].key == key) // the same key exists
            return i;
    }
    for (int i = 0; i < BARRIER_NUM; i++) 
    {
        if (barriers[i].key == 0) 
        {
            barriers[i].key = key;
            barriers[i].n = n;
            barriers[i].current_waiting = 0;
            init_list_head(&barriers[i].wait_queue);
            return i;
        }
    }
    return -1;
}

void do_barrier_wait(int bar_idx)
{
    if (bar_idx < 0 || bar_idx >= BARRIER_NUM || barriers[bar_idx].key == 0) return;
    barriers[bar_idx].current_waiting++;

    if (barriers[bar_idx].current_waiting >= barriers[bar_idx].n) 
    {
        // reach the goal, wake up and reset
        barriers[bar_idx].current_waiting = 0;
        while(do_unblock(&barriers[bar_idx].wait_queue)); 
    } 
    else 
    {
        int cpu_id = get_current_cpu_id();
        do_block(&current_running[cpu_id]->list, &barriers[bar_idx].wait_queue);
    }
}

void do_barrier_destroy(int bar_idx)
{
    if (bar_idx < 0 || bar_idx >= BARRIER_NUM) return;
    while(do_unblock(&barriers[bar_idx].wait_queue));   // won't happen theoretically

    barriers[bar_idx].key = 0;
    barriers[bar_idx].n = 0;
    barriers[bar_idx].current_waiting = 0;
    init_list_head(&barriers[bar_idx].wait_queue);
}

// =============== conditions ================
void init_conditions(void)
{
    for (int i = 0; i < CONDITION_NUM; i++) 
    {
        conditions[i].key = conditions[i].num_waiting = 0;
        init_list_head(&conditions[i].wait_queue);
    }
}

int do_condition_init(int key)
{
    for (int i = 0; i < CONDITION_NUM; i++) 
    {   // condition already exists
        if (conditions[i].key == key) 
            return i;
    }
    for (int i = 0; i < CONDITION_NUM; i++) 
    {
        if (conditions[i].key == 0) 
        {
            conditions[i].key = key;
            init_list_head(&conditions[i].wait_queue);
            return i;
        }
    }
    return -1;
}

void do_condition_wait(int cond_idx, int mutex_idx)
{
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM || conditions[cond_idx].key == 0) return;
    conditions[cond_idx].num_waiting++;
    do_mutex_lock_release(mutex_idx);

    int cpu_id = get_current_cpu_id();
    do_block(&current_running[cpu_id]->list, &conditions[cond_idx].wait_queue);
    do_mutex_lock_acquire(mutex_idx);
}

void do_condition_signal(int cond_idx)
{
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM || conditions[cond_idx].key == 0) return;
    if(conditions[cond_idx].num_waiting > 0)
    {
        conditions[cond_idx].num_waiting--;
        do_unblock(&conditions[cond_idx].wait_queue);
    }
}

void do_condition_broadcast(int cond_idx)
{
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM || conditions[cond_idx].key == 0) return;
    // wake up all waiting tasks
    while(do_unblock(&conditions[cond_idx].wait_queue));
}

void do_condition_destroy(int cond_idx)
{
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM) return;
    
    while(do_unblock(&conditions[cond_idx].wait_queue));
    conditions[cond_idx].key = conditions[cond_idx].num_waiting = 0;
    init_list_head(&conditions[cond_idx].wait_queue);
}