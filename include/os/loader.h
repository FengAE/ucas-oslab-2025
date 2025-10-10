#ifndef __INCLUDE_LOADER_H__
#define __INCLUDE_LOADER_H__

#include <type.h>

uint64_t load_task_img(int taskid);
uint64_t load_task_img_name(const char* name, task_info_t* tasks, int tasknum, bool is_batch);
#endif