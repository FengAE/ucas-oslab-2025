#include <os/lock.h>
#include <os/sched.h>
#include <os/list.h>
#include <atomic.h>
#include <assert.h> 
#include <os/string.h>
#include <os/smp.h> 

mailbox_t mboxes[MBOX_NUM];
static int mbox_global_mutex_idx;
#define MBOX_GLOBAL_LOCK_KEY 1000
void init_mbox(void)
{
    // Avoid conflicts with user program lock
    mbox_global_mutex_idx = do_mutex_lock_init(MBOX_GLOBAL_LOCK_KEY);

    for (int i = 0; i < MBOX_NUM; i++) 
    {
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
        int cpu_id = get_current_cpu_id();
        do_block(&current_running[cpu_id]->list, &mbox->send_queue);
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
        int cpu_id = get_current_cpu_id();
        do_block(&current_running[cpu_id]->list, &mbox->recv_queue);        
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
