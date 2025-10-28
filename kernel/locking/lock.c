#include <os/lock.h>
#include <os/sched.h>
#include <os/list.h>
#include <atomic.h>

mutex_lock_t mlocks[LOCK_NUM];

void init_locks(void)
{
    /* TODO: [p2-task2] initialize mlocks */
    for(int i = 0; i < LOCK_NUM; i++){
        mlocks[i].lock.status = UNLOCKED;
        mlocks[i].block_queue.next = &mlocks[i].block_queue;
        mlocks[i].block_queue.prev = &mlocks[i].block_queue;
        mlocks[i].key = 0;
    }
}

void spin_lock_init(spin_lock_t *lock)
{
    /* TODO: [p2-task2] initialize spin lock */
}

int spin_lock_try_acquire(spin_lock_t *lock)
{
    /* TODO: [p2-task2] try to acquire spin lock */
    return 0;
}

void spin_lock_acquire(spin_lock_t *lock)
{
    /* TODO: [p2-task2] acquire spin lock */
}

void spin_lock_release(spin_lock_t *lock)
{
    /* TODO: [p2-task2] release spin lock */
}

int do_mutex_lock_init(int key)
{
    /* TODO: [p2-task2] initialize mutex lock */
    for(int i=0; i<LOCK_NUM; i++)
    {
        if(mlocks[i].key == key)    return i; // already exists
    }
    for (int i=0; i<LOCK_NUM; i++) {
        if (mlocks[i].key == 0) 
        {  // free lock
            mlocks[i].lock.status = UNLOCKED;
            mlocks[i].block_queue.next = &mlocks[i].block_queue;
            mlocks[i].block_queue.prev = &mlocks[i].block_queue;
            mlocks[i].key = key;  
            return i;  
        }
    }
    return -1;  // no lock free
}

void do_mutex_lock_acquire(int mlock_idx)
{
    /* TODO: [p2-task2] acquire mutex lock */
    if(mlocks[mlock_idx].lock.status == UNLOCKED)
    {
        mlocks[mlock_idx].lock.status = LOCKED;
    }
    else
    {
        do_block(&current_running->list, &mlocks[mlock_idx].block_queue);
    }
}

void do_mutex_lock_release(int mlock_idx)
{
    /* TODO: [p2-task2] release mutex lock */
    //mlocks[mlock_idx].lock.status = UNLOCKED;
    if(!do_unblock(&(mlocks[mlock_idx].block_queue))) 
    {
        mlocks[mlock_idx].lock.status = UNLOCKED;
    }
}
