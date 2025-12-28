#ifndef __INCLUDE_OS_FS_H__
#define __INCLUDE_OS_FS_H__

#include <type.h>
#include <os/mm.h>
#include <os/task.h>

/* macros of file system */
#define SUPERBLOCK_MAGIC 0xDF4C4459
#define NUM_FDESCS 16


#define BLOCK_SIZE  4096
#define SEC_PER_BLOCK BLOCK_SIZE/SECTOR_SIZE
#define IM_DIR  0 
#define IM_FILE 1
#define MAGIC_NUM 0x66666666
#define FS_START_SECTOR 1048576

/* data structures of file system */
typedef struct superblock { // Describe whole file system
    // TODO [P6-task1]: Implement the data structure of superblock
    uint32_t magic;           
    uint32_t num_sector;         
    uint32_t start_sector;    
    
    uint32_t inode_map_offset; 
    uint32_t inode_map_sz;      
    uint32_t block_map_offset; 
    uint32_t block_map_sz;     
    uint32_t inode_offset;     
    uint32_t inode_sz;         
    uint32_t data_offset;      
    uint32_t data_sz;          
} superblock_t;

typedef struct dentry { // Describe name -> inode
    // TODO [P6-task1]: Implement the data structure of directory entry
    uint32_t inode_id;
    char name[32];
} dentry_t;

typedef struct inode {  // Describe specific file
    // TODO [P6-task1]: Implement the data structure of inode
    uint16_t mode;          // IM_DIR or IM_FILE
    uint16_t link_count;    // hard link num
    uint32_t indirect_block;
    uint64_t size;
} inode_t;

typedef struct fdesc {
    // TODO [P6-task2]: Implement the data structure of file descriptor
} fdesc_t;

/* modes of do_open */
#define O_RDONLY 1  /* read only open */
#define O_WRONLY 2  /* write only open */
#define O_RDWR   3  /* read/write open */

/* whence of do_lseek */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* fs function declarations */
extern int do_mkfs(void);
extern int do_statfs(void);
extern int do_cd(char *path);
extern int do_mkdir(char *path);
extern int do_rmdir(char *path);
extern int do_ls(char *path, int option);
extern int do_open(char *path, int mode);
extern int do_read(int fd, char *buff, int length);
extern int do_write(int fd, char *buff, int length);
extern int do_close(int fd);
extern int do_ln(char *src_path, char *dst_path);
extern int do_rm(char *path);
extern int do_lseek(int fd, int offset, int whence);

#endif