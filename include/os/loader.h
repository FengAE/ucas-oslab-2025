#ifndef __INCLUDE_LOADER_H__
#define __INCLUDE_LOADER_H__

#include <type.h>
#include <os/task.h>

uint64_t load_task_img(const char* name, task_info_t* tasks, int tasknum, uintptr_t pgdir);
// uint64_t excute_by_name(const char* name, task_info_t* tasks, int tasknum, bool is_batch);
#endif