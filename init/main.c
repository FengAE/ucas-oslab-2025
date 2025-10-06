#include <common.h>
#include <asm.h>
#include <os/kernel.h>
#include <os/task.h>
#include <os/string.h>
#include <os/loader.h>
#include <type.h>

#define VERSION_BUF 50
#define TASK_INFO_LOC 0x50200200
#define TASK_NUM_LOC 0x502001fe
#define TASK_INFO_START_LOC 0x502001f8
#define BATCH_START_SEC_LOC 0x502001f4
#define BUFFER 0x55000000


int version = 2; // version must between 0 and 9
char buf[VERSION_BUF];
int tasknum;

// Task info array
task_info_t tasks[TASK_MAXNUM];

static batch_file_t batchfiles __attribute__((aligned(512)));

static int bss_check(void)
{
    for (int i = 0; i < VERSION_BUF; ++i)
    {
        if (buf[i] != 0)
        {
            return 0;
        }
    }
    return 1;
}

static void init_jmptab(void)
{
    volatile long (*(*jmptab))() = (volatile long (*(*))())KERNEL_JMPTAB_BASE;

    jmptab[CONSOLE_PUTSTR]  = (long (*)())port_write;
    jmptab[CONSOLE_PUTCHAR] = (long (*)())port_write_ch;
    jmptab[CONSOLE_GETCHAR] = (long (*)())port_read_ch;
    jmptab[SD_READ]         = (long (*)())sd_read;
}

static void init_task_info(void)
{
    // TODO: [p1-task4] Init 'tasks' array via reading app-info sector
    // NOTE: You need to get some related arguments from bootblock first
    tasknum = *((short*)TASK_NUM_LOC);  // bootblock stage load: image->memory
    int task_info_start_sec = *((int*)TASK_INFO_START_LOC);

    unsigned sec_num = (unsigned)NBYTES2SEC(sizeof(task_info_t)*tasknum);
    bios_sd_read((unsigned int)tasks, sec_num, task_info_start_sec);
    // bios_sd_read((unsigned int)BUFFER, sec_num, task_info_start_sec);
    // memcpy((void*)tasks, (void*)BUFFER, sizeof(task_info_t)*tasknum);
    bios_putstr("loaded apps: \n\r");
    for(int i=0; i<tasknum; i++)
    {
        bios_putstr(tasks[i].name);
        bios_putstr("\n\r");
    }
    bios_putstr("\n\r");
}

static void load_batchfiles()
{
    int batch_start_sec = *((int*)BATCH_START_SEC_LOC);
    int ch, name_ptr = 0;
    char name[16];
    batchfiles.num = 0;
    while(1)
    {
        while((ch = bios_getchar()) == -1);
        if(ch == '\r' || ch == '\n')
        {
            bios_putstr("\n\r");
            break;
        }
        else
        {
            if(ch == ' ')
            {
                if(name_ptr == 0)
                    continue;
                else 
                {
                    name[name_ptr] = '\0';
                    if(batchfiles.num >= BATCH_MAXNUM)
                    {
                        bios_putstr("batch file num exceed max limit\n\r");
                        return;
                    }
                    strcpy(batchfiles.names[batchfiles.num++], name);
                    name_ptr = 0;
                }
            }
            else 
            {
                if(name_ptr >= 16)
                {
                    bios_putstr("\n\r");
                    bios_putstr("input task name too long\n\r");
                    name_ptr = 0;
                }
                else
                    name[name_ptr++] = ch;
            }
            bios_putchar(ch);
        }
    }
    bios_sd_write((unsigned int)&batchfiles, 1, batch_start_sec);       
}


void excute_batchfiles(task_info_t* tasks, int tasknum)
{
    if(tasknum == 0)
    {
        bios_putstr("No batch files loaded!\n\r");
        return;
    }
    int batch_start_sec = *((int*)BATCH_START_SEC_LOC);
    // Load batch first in buffer, else might cause tasks data covered!!
    bios_sd_read((unsigned int)BUFFER, 1, batch_start_sec);
    memcpy((void*)&batchfiles, (void*)BUFFER, sizeof(batch_file_t));
    for(int i=0; i<tasknum; i++)
    {
        bios_putstr(tasks[i].name);
        bios_putstr("\n\r");
    }
    bios_putstr("\n\r");

    for(int i=0; i<batchfiles.num; i++)
    {
        bios_putstr("excute batch file: ");
        bios_putstr(batchfiles.names[i]);
        bios_putstr("\n\r");
        load_task_img_name(batchfiles.names[i], tasks, tasknum);
    }
}

/************************************************************/
/* Do not touch this comment. Reserved for future projects. */
/************************************************************/

int main(void)
{
    // Check whether .bss section is set to zero
    int check = bss_check();

    // Init jump table provided by kernel and bios(ΦωΦ)
    init_jmptab();

    // Init task information (〃'▽'〃)
    init_task_info();

    // Output 'Hello OS!', bss check result and OS version
    char output_str[] = "bss check: _ version: _\n\r";
    char output_val[2] = {0};
    int i, output_val_pos = 0;

    output_val[0] = check ? 't' : 'f';
    output_val[1] = version + '0';
    for (i = 0; i < sizeof(output_str); ++i)
    {
        buf[i] = output_str[i];
        if (buf[i] == '_')
        {
            buf[i] = output_val[output_val_pos++];
        }
    }

    bios_putstr("Hello OS!\n\r");
    bios_putstr(buf);

    // [p1-task2]: get keyboard input to display on screen
    // int ch;
    // while(1)
    // {
    //     while((ch = bios_getchar())==-1);
    //     bios_putchar(ch);
    // }

    // // [p1-task3]: Load tasks by task id and then execute them.
    // int ch, taskid = 0;
    // while(1)
    // {
    //     while((ch = bios_getchar()) == -1);
    //     if(ch == '\r' || ch == '\n')
    //     {
    //         bios_putstr("\n\r");
    //         if(taskid>0 && taskid<=TASK_MAXNUM)
    //             // load_task_img(taskid);   false: only copy fun to mem, not excute
    //             ((void (*)())load_task_img(taskid))();
    //         else
    //             bios_putstr("taskid not valid\n\r");
    //         taskid = 0;
    //     }
    //     else 
    //     {
    //         bios_putchar(ch);
    //         if(ch >= '0' && ch<='9')
    //         {
    //             taskid *= 10;
    //             taskid += ch-'0';
    //         }
    //         else
    //         {
    //             bios_putstr("\n\r");
    //             bios_putstr("input charator not valid, expecting number 0~9\n\r");
    //             taskid = 0;
    //         }
    //     }      
    // }

    // [p1-task4]: Load tasks by task name and then execute them.
    char name[16];
    name[0] = '\0';
    int name_ptr = 0, ch;
    while(1)
    {
        while((ch = bios_getchar()) == -1);
        if(ch == '\r' || ch == '\n')
        {
            int flag = 0;
            bios_putstr("\n\r");
            if(name_ptr != 0)
            {
                name[name_ptr] = '\0';
                // if(strcmp(name, "load_bat") == 0)
                //     load_batchfiles();
                // else 
                if(strcmp(name, "do_bat") == 0)
                    excute_batchfiles(tasks, tasknum);
                else 
                    load_task_img_name(name, tasks, tasknum);
            }
            else
                bios_putstr("Input empty!\n\r");
            name_ptr = 0;
        }
        else 
        {
            bios_putchar(ch);
            if(name_ptr >= 16)
            {
                bios_putstr("\n\r");
                bios_putstr("input task name too long\n\r");
                name_ptr = 0;
            }
            else
                name[name_ptr++] = ch;
        }      
    }


    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    while (1)
    {
        asm volatile("wfi");
    }

    return 0;
}
