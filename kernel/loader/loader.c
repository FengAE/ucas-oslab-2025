#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <os/mm.h>
#include <type.h>
#define EVERY_APP_SEC 15

#define BUFFER 0xffffffc059000000

uint64_t load_task_img(const char* name, task_info_t* tasks, int tasknum, uintptr_t pgdir)
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

    task_info_t *info = &tasks[id];
    uint64_t va_start = info->entry;         // 0x10000
    uint64_t va_end   = va_start + info->memsz; 
    uint64_t file_offset = info->offset;

    for (uint64_t va = va_start; va < va_end; va += PAGE_SIZE) 
    {
        uintptr_t page_kva = alloc_page_helper(va, pgdir);
        uint64_t prog_off = va - va_start;

        int len_to_read = 0;
        if (prog_off < info->size) 
        {
            uint64_t remain = info->size - prog_off;
            len_to_read = remain >= PAGE_SIZE ? PAGE_SIZE : remain;
        }

        if (len_to_read > 0) 
        {
            uint64_t abs_file_off = file_offset + prog_off;
            uint64_t cur_sec = abs_file_off / SECTOR_SIZE;
            uint64_t cur_off = abs_file_off % SECTOR_SIZE;

            int sec_num = (cur_off + len_to_read + SECTOR_SIZE - 1) / SECTOR_SIZE;
            bios_sd_read(BUFFER, sec_num, cur_sec);
            memcpy((void*)page_kva, (void*)(BUFFER + cur_off), len_to_read);

            if (len_to_read < PAGE_SIZE)    // remain: 0
                memset((void*)(page_kva + len_to_read), 0, PAGE_SIZE - len_to_read);

        } else 
            // pure BSS
            memset((void*)page_kva, 0, PAGE_SIZE);
    }

    return info->entry;
}

//2. [p1-task4] load task via task name, thus the arg should be 'char *taskname'
// void excute_by_name(const char* name, task_info_t* tasks, int tasknum, bool is_batch)
// {
//     uint64_t entry = load_task_img(name, tasks, tasknum);

//     int argc = 2;
//     char* argv[argc];
//     argv[0] = "./";    // argv[0]: ./taskname  here omit
//     if(is_batch)
//         argv[1] = "1";
//     else 
//         argv[1] = "0";
//     if(entry != 0)
//         ((void (*)())entry)(argc, argv); 
// }
