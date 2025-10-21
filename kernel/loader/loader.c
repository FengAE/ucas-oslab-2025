#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>
#define EVERY_APP_SEC 15

#define BUFFER 0x59000000

uint64_t load_task_img(const char* name, task_info_t* tasks, int tasknum)
{
    int flag = 0, id = 0;
    for(int i=0; i<tasknum; i++)
    {
        if(strcmp(tasks[i].name, name) == 0)
        {
            flag = 1, id = i;
            break;
        }
    }
    if(!flag)   
    {
        bios_putstr("App name input error!\n\r");
        return 0;
    }
    
    uint64_t entry = tasks[id].entry;
    bios_sd_read(BUFFER, 
                (tasks[id].offset+tasks[id].size)/SECTOR_SIZE - tasks[id].offset/SECTOR_SIZE + 1, 
                tasks[id].offset/SECTOR_SIZE);
    memcpy((void*)entry, (void*)(BUFFER + tasks[id].offset%SECTOR_SIZE), tasks[id].size);
    return entry; 
}

//2. [p1-task4] load task via task name, thus the arg should be 'char *taskname'
void load_task_img_name(const char* name, task_info_t* tasks, int tasknum, bool is_batch)
{
    uint64_t entry = load_task_img(name, tasks, tasknum);

    int argc = 2;
    char* argv[argc];
    argv[0] = "./";    // argv[0]: ./taskname  here omit
    if(is_batch)
        argv[1] = "1";
    else 
        argv[1] = "0";
    ((void (*)())entry)(argc, argv); 
}