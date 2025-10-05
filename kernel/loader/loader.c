#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>
#define EVERY_APP_SEC 15

#define BUFFER 0x55000000

uint64_t load_task_img(int taskid)
{
    //TODO:
    //1. [p1-task3] load task from image via task id, and return its entrypoint
    uint64_t entry = TASK_MEM_BASE + TASK_SIZE*(taskid-1);
    bios_sd_read(entry, EVERY_APP_SEC, 1+taskid*EVERY_APP_SEC);
    return entry;    
}

//2. [p1-task4] load task via task name, thus the arg should be 'char *taskname'
uint64_t load_task_img_name(int id, task_info_t* tasks)
{
    uint64_t entry = tasks[id].entry;
    // False method:   size exceed might cause truly_illegal_insn
    // bios_sd_read(entry, NBYTES2SEC(tasks[id].size)+1, NBYTES2SEC(tasks[id].offset)-1);
    // return entry + tasks[id].offset%SECTOR_SIZE;

    bios_sd_read(BUFFER, 
                (tasks[id].offset+tasks[id].size)/SECTOR_SIZE - tasks[id].offset/SECTOR_SIZE + 1, 
                tasks[id].offset/SECTOR_SIZE);
    memcpy((void*)entry, (void*)(BUFFER + tasks[id].offset%SECTOR_SIZE), tasks[id].size);
    return entry;
}