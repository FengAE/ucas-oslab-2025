#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <mailbox.h>
#define DATA_SIZE 10
typedef int pthread_t;

typedef struct 
{
    int mbox_id;
    int base_row;
} thread_arg_t;


void thread_sender(void *arg)
{
    int mbox_id = (int)(uint64_t)arg;
    char data[DATA_SIZE];
    memset(data, 'S', DATA_SIZE);
    int i=0;
    
    while(1) 
    {
        sys_move_cursor(0, ((thread_arg_t*)arg)->base_row);
        // Send once space free
        printf("Trying to send data [%d]\n", i);
        sys_mbox_send(mbox_id, data, DATA_SIZE);
        printf("Finish send data [%d]\n", i++);
    }
    sys_thread_exit(); 
}

void thread_receiver(void *arg)
{
    int mbox_id = (int)(uint64_t)arg;
    char data[DATA_SIZE];
    int i=0;
    
    while(1) 
    {
        sys_move_cursor(0, ((thread_arg_t*)arg)->base_row);
        // Recieve once data exists
        printf("Trying to recieve data [%d]\n", i);
        sys_mbox_recv(mbox_id, data, DATA_SIZE);
        printf("Finish recieve data [%d]\n", i++);
    }
    sys_thread_exit();
}

void task_a(int mbox1, int mbox2)
{
    sys_move_cursor(0, 1);
    printf("[A] Spawning threads: Send->M1, Recv<-M2\n");
    pthread_t t_recv, t_send;
    thread_arg_t recv_arg = {mbox2, 2};
    thread_arg_t send_arg = {mbox1, 4};

    sys_thread_create(&t_recv, thread_receiver, (void*)(&recv_arg));
    sys_thread_create(&t_send, thread_sender, (void*)(&send_arg));    
    sys_thread_join(t_recv);
    sys_thread_join(t_send);
}

void task_b(int mbox1, int mbox2)
{
    sys_move_cursor(0, 7);
    printf("[B] Spawning threads: Send->M2, Recv<-M1\n");
    pthread_t t_recv, t_send;
    thread_arg_t recv_arg = {mbox2, 8};
    thread_arg_t send_arg = {mbox1, 10};

    sys_thread_create(&t_recv, thread_receiver, (void*)(&recv_arg));
    sys_thread_create(&t_send, thread_sender, (void*)(&send_arg));  
    sys_thread_join(t_recv);
    sys_thread_join(t_send);
}

int main(int argc, char *argv[])
{
    if (argc == 1) 
    {
        int m1 = sys_mbox_open("mbox1");
        int m2 = sys_mbox_open("mbox2");
        if (strcmp(argv[0], "A") == 0) task_a(m1, m2);
        if (strcmp(argv[0], "B") == 0) task_b(m1, m2);
        sys_exit();
        return 0;
    }

    // Create and fill mboxes
    int m1 = sys_mbox_open("mbox1");
    int m2 = sys_mbox_open("mbox2");
    
    char fill_data[DATA_SIZE];
    memset(fill_data, 'C', DATA_SIZE);
    int i;
    for (i = 0; i < 6; i++) sys_mbox_send(m1, fill_data, DATA_SIZE);
    for (i = 0; i < 6; i++) sys_mbox_send(m2, fill_data, DATA_SIZE);
    sys_move_cursor(0, 0);
    printf("Mailboxes filled. Launch A and B...\n");

    char *argv_a[] = {"A"};
    char *argv_b[] = {"B"};
    
    sys_exec("deadlock_solve", 1, argv_a);
    sys_exec("deadlock_solve", 1, argv_b);

    sys_exit();
    return 0;
}