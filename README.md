## 代码框架

```apl
arch/riscv/	# 架构相关
├── boot/          
│   └── bootblock.S  # Bootloader主程序
├── kernel/          
│   └── head.S       # 内核入口点
├── crt0/            
│   └── crt0.S       # 用户程序入口点
├── bios/         
│   └── common.c     # BIOS函数封装
└── include/         # 架构相关头文件
    ├── asm/       
    │   └── biosdef.h # BIOS函数ID定义
    ├── asm.h        # 汇编宏定义
    ├── common.h     # 通用定义，对应common.c
    └── csr.h        # 控制状态寄存器定义

kernel/	# 内核
└── loader/          
    └── loader.c     # 用户程序加载实现
init/
└── main.c          # 内核主程序
libs/
└── string.c        # 字符串处理函数库

include/
├── os/           
│   ├── kernel.h    # 内核接口定义，定义了调用bios函数的跳转表
│   ├── loader.h   
│   ├── sched.h     # 调度器接口
│   ├── string.h   
│   └── task.h      # 任务管理定义
└── type.h          # 基础类型定义

tools/
└── createimage.c   # 镜像创建工具源码
build/	# 构建目录
```





## task1：制作第一个引导块

任务是在`bootblock.S`中调用`BIOS_API`，输出特定字符串。

```assembly
launch_msg: .string "It's fengxiyi's bootloader\n\r"
	li		a7, BIOS_PUTSTR
	la 		a0, launch_msg			
	call	bios_func_entry
```

只需要调用在`common.c`的`putstr`函数即可。由于各个函数API用**跳转表**进行索引，故而需要填入偏移。查询可知偏移需要填入`a7`寄存器





## task2：加载和初始化内存

### 1. 加载并跳转内核代码

在`bootblock.S`中调用`BIOS_API`，将SD卡的内核代码段（起始于**第二个扇区**）搬运到内存中。（同task1，直接调用`sd_read`函数即可）

```assembly
# load os size from os_size_loc
	li 		a0, os_size_loc
	lh 		a1, 0(a0)
	
	li		a7, BIOS_SDREAD
	la		a0, kernel	
	li		a2, 1
	call	bios_func_entry		# use call, not j 
	j		kernel
```

由于已经给出的image文件已经将`os_size`写入偏移`0x01fc`处，U-Boot加载`bootblock`后会**自动加载**到`os_size_loc`（0x502001fc）处，只需直接获取对应地址的值即可获得kernel所占扇区大小。



### 2. 初始化内存

在跳转到kernel后，需要在**内核启动入口设置内存**，才能进入main。

（1）清空`.bss`段。

这一步是确保此后未初始化变量都被初始化为0。其中`__bss_start`和`__BSS_END__`是链接脚本**`riscv.lds`**中定义的符号， 直接引用即可。

```assembly
	la    	t0, __bss_start
  	la    	t1, __BSS_END__
clear_bss:
  	sw    	zero, 0(t0)
  	addi  	t0, t0, 4
  	blt   	t0, t1, clear_bss
```

（2）设置栈指针

`KERNEL_STACK`为0x50500000，已经规定好的内核栈指针初始位置。

```assembly
    la      sp, KERNEL_STACK    
    call    main                
```



### 3. 输出回显

在main中添加回显代码即可。注意，由于`bios_getchar`在没有获取键盘输入时，返回-1，需要逻辑跳过。

```c
int ch;
while(1)
{
    while((ch = bios_getchar())==-1);
    bios_putchar(ch);
}
```





## task3：加载启动用户程序（编号索引）

**框架流程**：

（1）`makefile`构建用户程序可执行文件

```makefile
# establish app
$(DIR_BUILD)/%: $(DIR_TEST_PROJ)/%.c $(OBJ_CRT0) riscv.lds
	$(CC) $(USER_CFLAGS) -o $@ $(OBJ_CRT0) $< -Wl,--defsym=TEXT_START=$(USER_ENTRYPOINT) -T riscv.lds
```

（2）构建镜像

```makefile
image: $(ELF_CREATEIMAGE) $(ELF_BOOT) $(ELF_MAIN) $(ELF_USER)
	cd $(DIR_BUILD) && ./$(<F) --extended $(filter-out $(<F), $(^F))	# equal to: cd build && ./createimage --extended bootblock main 2048 auipc bss data
```

（3）main程序调用用户程序（根据地址）



### 1. `createimage`写入程序内容

（1）**扇区对齐**写入

注意：`bootblock`为1扇区对齐，其余均为15扇区对齐

```c
if (strcmp(*files, "bootblock") == 0) {
    write_padding(img, &phyaddr, SECTOR_SIZE);
    cur_tail += SECTOR_SIZE;
}
else
{
    cur_tail += EVERY_APP_SEC_NUM * SECTOR_SIZE;
    write_padding(img, &phyaddr, cur_tail);
}
```

（2）写入`os_size`（供`bootblock.S`）

```c
short os_sec = (short)NBYTES2SEC(nbytes_kernel);
fseek(img, OS_SIZE_LOC, SEEK_SET)
fwrite(&os_sec, sizeof(short), 1, img);
```



### 2. main调用用户程序

需要支持在终端输入编号，执行对应程序。获取终端输入使用`bios_getchar`即可，在**获取输入编号`taskid`**后，调用loader函数、获取对应程序地址。

```c
// load_task_img(taskid): app entry address
((void (*)())load_task_img(taskid))();
```

loader函数需要返回app程序地址。`sd_read`能读入扇区到指定内存地址，返回该地址即可。由于在写入image时**已经做了15扇区对齐**，所以起始扇区的访问比较简单。

```c
uint64_t load_task_img(int taskid)
{
    //[p1-task3] load task from image via task id, and return its entrypoint
    uint64_t entry = TASK_MEM_BASE + TASK_SIZE*(taskid-1);
    bios_sd_read(entry, EVERY_APP_SEC, 1+taskid*EVERY_APP_SEC);
    return entry;    
}
```





## task4: 无空泡加载启动用户程序（名称索引）

### 1. `createimage`写入程序信息

对于每个用户程序，由于后续需要进行调用，所以不仅需要存程序内容，还需要存程序**名字、偏移、大小等**信息。

设计`taskinfo`结构体如下：

```c
typedef struct {
    uint64_t    entry;
    int         offset;
    int         size;
    char        name[16];
} task_info_t;
```

（1）**读取elf文件**时，填入task相关信息

```c
if(taskidx >= 0)	// ensure is app
{
    taskinfo[taskidx].entry = ehdr.e_entry;
    strcpy(taskinfo[taskidx].name, *files);
    taskinfo[taskidx].offset = phyaddr;
    taskinfo[taskidx].is_batch = 0;
}
// ... write app data
if(taskidx >= 0)
    taskinfo[taskidx].size = phyaddr - taskinfo[taskidx].offset;
```

（2）无空泡写入image

只对`bootblock`对齐即可

```c
if (strcmp(*files, "bootblock") == 0) 
     write_padding(img, &phyaddr, SECTOR_SIZE);
```

（3）写入程序info

考虑到写入kernel、用户程序都已经在前面函数封装好、不便于改动，故而在写`taskinfo`时可以**直接写在镜像末尾**。

但是后续怎么进行读取呢？这需要提供**起始扇区编号**。所以还需要像存`os_size`一样，存储`taskinfo`的起始扇区编号`task_start_sec`。

同样，后续需要遍历访问`taskinfo`的名称信息。如果使用for循环的话，终止条件怎么确定？这还需要存入**程序数量**`tasknum`。

```c
int task_start_sec = (int)NBYTES2SEC(phyaddr);    

// write taskinfo in the end of image
write_padding(img, &phyaddr, task_start_sec*SECTOR_SIZE);
fwrite(taskinfo, sizeof(task_info_t), tasknum, img);

// write task_start_sec: 0x01f8~0x1fc
fseek(img, TASK_START_SEC_LOC*SECTOR_SIZE, SEEK_SET);
fwrite(&task_start_sec, sizeof(int), 1, img);

// write os size: 0x01fc~0x01fe
fwrite(&os_sec, sizeof(short), 1, img);

// write tasknum: 0x01fe~0x0200
fwrite(&tasknum, sizeof(short), 1, img);
```



### 2. main调用用户程序

基本的获取终端输入在此省略。假设已经在获取终端输入名称`name`，下一步怎么执行对应程序呢？

和task3一样，我们需要调用一个loader函数，将对应程序从镜像加载到内存、跳转对应起始内存地址执行即可。



但是，程序没有进行扇区对齐，而`sd_read`只能读取指定的几个扇区。所以我们需要使用此前`taskinfo`存入的**offset（在image的偏移）、size**信息

```c
uint64_t load_task_img_name(const char* name, task_info_t* tasks, int tasknum)
{
    // ......
	// 假设此前已经通过name获取编号id
	bios_sd_read(BUFFER, 
	                (tasks[id].offset+tasks[id].size)/SECTOR_SIZE - tasks[id].offset/SECTOR_SIZE + 1, 
	                tasks[id].offset/SECTOR_SIZE);
	    memcpy((void*)entry, (void*)(BUFFER + tasks[id].offset%SECTOR_SIZE), tasks[id].size);
	return entry;
}
```

在此需要将image信息**先读入buffer再根据大小，copy到指定内存entry**，这是因为初始read的时候只能读取扇区、容易读入冗余信息，直接放到内存的话**可能会覆盖其他片段**。





## task5: 批处理程序

### 1. 批处理文件设计

设计批处理文件结构如下：

```c
typedef struct {
    int         num;      // actual number of programs 
    char        names[BATCH_MAXNUM][16]; 
} batch_file_t;

static batch_file_t batchfiles;
```

和task4一样，需要存储批处理文件的信息、起始扇区。

```c
write_padding(img, &phyaddr, batch_start_sec*SECTOR_SIZE);
fseek(img, batch_start_sec*SECTOR_SIZE, SEEK_SET);
printf("write batch files at sec: %d\n", batch_start_sec);
fwrite(&batchfiles, sizeof(batch_file_t), 1, img);

// write batch_start_sec: 0x01f4~0x01f8
fseek(img, BATCH_BEGIN_SEC_LOC, SEEK_SET);
fwrite(&batch_start_sec, sizeof(int), 1, img);
```



### 2. 批处理程序执行

对于批处理程序，要求输入为上一个程序的输出。这需要我们对用户程序的编写给出两套标准：

（1）为普通程序时：使用默认值

（2）为批处理程序时，**从特定地址`BATCH_DATA_BUFFER`获取数据**（后续需要将输出结果也写入该地址）

怎么区分是否为批处理？为了保证`int main()`传参不产生冲突，我决定手动传入`argc`、 `argv`，并以`argv[1]`为区分：

```c
int argc = 2;
char* argv[argc];
argv[0] = "./";    // argv[0]: ./taskname  here omit
if(is_batch)
    argv[1] = "1";
else 
    argv[1] = "0";
((void (*)())entry)(argc, argv); 
```

对编写的测试程序，进行区分、分别加载数据即可

```c
int main(int argc, char* argv[])
{
    int data;
    if(argv[1][0] == '1')   // bat
        data = *(int*)BATCH_DATA_LOC;
    else    // not bat
        data = 2;
    // deal .......
}
```



### 3. 批处理文件写入

需要支持，在终端输入`load_bat`时，能获取以空格为分割的文件名称，依次存入批处理文件。

获取输入省略，只需填充`batchfile`，再使用`bios_sd_write`写入对应位置更新即可。

```c
bios_sd_write((uint64_t)&batchfiles, 1, batch_start_sec);
```

