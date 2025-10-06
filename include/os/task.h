#ifndef __INCLUDE_TASK_H__
#define __INCLUDE_TASK_H__

#include <type.h>

#define TASK_MEM_BASE    0x52000000
#define TASK_MAXNUM      16
#define TASK_SIZE        0x10000


#define SECTOR_SIZE 512
#define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0))

/* TODO: [p1-task4] implement your own task_info_t! */
typedef struct {
    uint64_t    entry;
    int         is_batch;
    int         offset;
    int         size;
    char        name[16];
} task_info_t;

#define BATCH_MAXNUM 16

typedef struct {
    int         num;     
    char        names[BATCH_MAXNUM][16]; 
} batch_file_t;

extern task_info_t tasks[TASK_MAXNUM];

#endif