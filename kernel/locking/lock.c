#include <os/lock.h>
#include <os/sched.h>
#include <os/list.h>
#include <atomic.h>
#include <os/string.h>

mutex_lock_t mlocks[LOCK_NUM];
barrier_t barriers[BARRIER_NUM];
condition_t conditions[CONDITION_NUM];
mailbox_t mboxes[MBOX_NUM];

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
    if(mlocks[mlock_idx].lock.status == UNLOCKED)
    {
        current_running->lock_id = mlock_idx;
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

// ]================== barriers ================
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
        do_block(&current_running->list, &barriers[bar_idx].wait_queue);
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

    do_block(&current_running->list, &conditions[cond_idx].wait_queue);
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


// ============== mailbox ==================
static int mbox_global_mutex_idx;
#define MBOX_GLOBAL_LOCK_KEY 1000
void init_mbox(void)
{
    // Avoid conflicts with user program lock
    mbox_global_mutex_idx = do_mutex_lock_init(MBOX_GLOBAL_LOCK_KEY);

    for (int i = 0; i < MBOX_NUM; i++) {
        mboxes[i].name[0] = '\0';
        mboxes[i].head = 0;
        mboxes[i].tail = 0;
        mboxes[i].nbytes = 0;
        mboxes[i].user_count = 0;
        mboxes[i].mutex_idx = -1; // Invalid mutex
        init_list_head(&mboxes[i].send_queue);
        init_list_head(&mboxes[i].recv_queue);
    }
}

int do_mbox_open(char *name)
{
    do_mutex_lock_acquire(mbox_global_mutex_idx);
    // Try to find existing mailbox
    for (int i = 0; i < MBOX_NUM; i++) 
    {
        if (mboxes[i].user_count > 0 && strcmp(mboxes[i].name, name) == 0) 
        {
            mboxes[i].user_count++;
            do_mutex_lock_release(mbox_global_mutex_idx);
            return i;
        }
    }
    // Not found --> create new mailbox
    for (int i = 0; i < MBOX_NUM; i++) 
    {
        if (mboxes[i].user_count == 0) 
        {
            strcpy(mboxes[i].name, name);
            mboxes[i].head = 0;
            mboxes[i].tail = 0;
            mboxes[i].nbytes = 0;
            mboxes[i].user_count = 1;
            init_list_head(&mboxes[i].send_queue);
            init_list_head(&mboxes[i].recv_queue);
            mboxes[i].mutex_idx = do_mutex_lock_init(MBOX_GLOBAL_LOCK_KEY + 1 + i);
            do_mutex_lock_release(mbox_global_mutex_idx);
            return i;
        }
    }
    do_mutex_lock_release(mbox_global_mutex_idx);
    return -1; // Full
}

void do_mbox_close(int mbox_idx)
{
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) return;
    do_mutex_lock_acquire(mbox_global_mutex_idx);
    
    if (mboxes[mbox_idx].user_count > 0) 
    {
        mboxes[mbox_idx].user_count--;    
        if (mboxes[mbox_idx].user_count == 0) 
        {
            mboxes[mbox_idx].name[0] = '\0';            
            while(do_unblock(&mboxes[mbox_idx].send_queue));
            while(do_unblock(&mboxes[mbox_idx].recv_queue));
        }
    }
    do_mutex_lock_release(mbox_global_mutex_idx);
}

int do_mbox_send(int mbox_idx, void *msg, int msg_length)
{
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM || msg == NULL) return -1;
    if (msg_length > MAX_MBOX_LENGTH) return -1;

    mailbox_t *mbox = &mboxes[mbox_idx];
    int blocked_times = 0;
    char* data = (char*)msg;

    // Acquire the mailbox mutex
    do_mutex_lock_acquire(mbox->mutex_idx);
    // Wait until there is enough space
    while (MAX_MBOX_LENGTH - mbox->nbytes < msg_length) 
    {
        blocked_times++;
        do_mutex_lock_release(mbox->mutex_idx);
        do_block(&current_running->list, &mbox->send_queue);
        do_mutex_lock_acquire(mbox->mutex_idx);
    }

    // Perform write
    for (int i = 0; i < msg_length; i++) 
    {
        mbox->buffer[mbox->tail] = data[i];
        mbox->tail = (mbox->tail + 1) % MAX_MBOX_LENGTH;
    }
    mbox->nbytes += msg_length;
    // Wake up receivers
    while(do_unblock(&mbox->recv_queue));

    do_mutex_lock_release(mbox->mutex_idx);
    return blocked_times;
}

int do_mbox_recv(int mbox_idx, void *msg, int msg_length)
{
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM || msg == NULL) return -1;
    mailbox_t *mbox = &mboxes[mbox_idx];
    int blocked_times = 0;
    char* data = (char*)msg;
    do_mutex_lock_acquire(mbox->mutex_idx);

    // Wait until there is enough data
    while (mbox->nbytes < msg_length) 
    {
        blocked_times++;
        do_mutex_lock_release(mbox->mutex_idx);
        do_block(&current_running->list, &mbox->recv_queue);        
        do_mutex_lock_acquire(mbox->mutex_idx);
    }

    // Perform Read
    for (int i = 0; i < msg_length; i++) 
    {
        data[i] = mbox->buffer[mbox->head];
        mbox->head = (mbox->head + 1) % MAX_MBOX_LENGTH;
    }
    mbox->nbytes -= msg_length;
    // Wake up senders
    while(do_unblock(&mbox->send_queue));
    do_mutex_lock_release(mbox->mutex_idx);

    return blocked_times;
}