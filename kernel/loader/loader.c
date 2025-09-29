#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>
#define EVERY_APP_SEC 15

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
    bios_sd_read(entry, NBYTES2SEC(tasks[id].size), NBYTES2SEC(tasks[id].offset));
    return entry;
}