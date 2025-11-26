#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <mailbox.h>

#define MBOX1_KEY 1
#define MBOX2_KEY 2
#define DATA_SIZE 10 

void task_a(int mbox1, int mbox2)
{
    char data[DATA_SIZE];
    memset(data, 'A', DATA_SIZE);
    
    // To force deadlock , we must send first.
    while(1)
    {
        sys_move_cursor(0, 1);
        printf("[A] Trying to send to mbox1\n");
        sys_mbox_send(mbox1, data, DATA_SIZE);
        printf("[A] Sent to mbox1!\n");

        sys_move_cursor(0, 2);
        printf("[A] Trying to recv from mbox2\n");
        sys_mbox_recv(mbox2, data, DATA_SIZE);
        printf("[A] Recv from mbox2!\n");
    }
}

void task_b(int mbox1, int mbox2)
{
    char data[DATA_SIZE];
    memset(data, 'B', DATA_SIZE);

    while(1)
    {
        sys_move_cursor(0, 4);
        printf("[B] Trying to send to mbox2\n");
        sys_mbox_send(mbox2, data, DATA_SIZE);
        printf("[B] Sent to mbox2!\n");

        sys_move_cursor(0, 5);
        printf("[B] Trying to recv from mbox1\n");
        sys_mbox_recv(mbox1, data, DATA_SIZE);
        printf("[B] Recv from mbox1!\n");
    }
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
    // since mbox size is 64
    // firstly fill almost full, else the whole program will be locked
    for (i = 0; i < 6; i++) sys_mbox_send(m1, fill_data, DATA_SIZE);
    for (i = 0; i < 6; i++) sys_mbox_send(m2, fill_data, DATA_SIZE);
    
    printf("Mailboxes filled. Launch A and B...\n");

    char *argv_a[] = {"A"};
    char *argv_b[] = {"B"};
    
    sys_exec("deadlock", 1, argv_a);
    sys_exec("deadlock", 1, argv_b);

    sys_exit();
    return 0;
}