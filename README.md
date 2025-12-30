## Task 1

### 1. mkfs 初始化

- 初始化 superblock、两张 bitmap 与 inode table
- 创建根目录并写入 `.` / `..`

### 2. statfs

- 读取 bitmap 统计 inode/block 使用量
- 输出总量/已用/剩余

### 3. mkdir / rmdir

- mkdir：分配 inode + 数据块 + dentry，写入 `.` / `..`
- rmdir：只允许空目录删除（仅 `.`/`..`）

### 4. cd / ls

- cd 更新当前进程 cwd inode
- ls 输出目录项；`-l` 打印 inode/链接数/大小





## Task 2

### 1. 文件描述符 (fd) 语义

- open 返回 fd，记录 inode/offset/权限

- read/write 根据 offset 前进，close 释放 fd

  

### 2. open / read / write / close

- read 越界返回 0，不修改 offset

- write 扩容时更新 inode size

- 写入需及时落盘保证持久化

- read时如果有空洞，不进行数据块填补；但是write需要

  

### 3. touch / cat / ln / rm

- touch：通过写模式 open 创建空文件

- cat：顺序 read 并打印

- ln：增加硬链接计数

- rm：仅删除 dentry；链接数归零才释放 inode 与数据块

  

### 4. 大文件与 lseek

- 采用双层间接索引扩展寻址空间
- 支持 SEEK_SET / SEEK_CUR / SEEK_END
- 支持空洞文件，lseek 可跳过中间块