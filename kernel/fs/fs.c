#include <os/string.h>
#include <os/fs.h>
#include<printk.h>

static fdesc_t fdesc_array[NUM_FDESCS];
static superblock_t sb; // global superblock
static uint32_t current_dir_inode_id = 1;   // root

uint8_t block_map[32 * 512]; 
uint8_t inode_map[SECTOR_SIZE];
uint8_t temp_buf[BLOCK_SIZE] __attribute__((aligned(BLOCK_SIZE)));

uint32_t get_block_sector(uint32_t block_id) 
{
    return FS_START_SECTOR + sb.data_offset + block_id * SEC_PER_BLOCK;
}

// read inode_id inode to memory
void read_inode(uint32_t inode_id, inode_t *inode) 
{
    int offset = inode_id * sizeof(inode_t);
    int sector = FS_START_SECTOR + sb.inode_offset + offset / SECTOR_SIZE;
    int off_in_sector = offset % SECTOR_SIZE;
    
    uint8_t buf[SECTOR_SIZE];
    bios_sd_read(kva2pa(buf), 1, sector);
    memcpy(inode, buf + off_in_sector, sizeof(inode_t));
}

void write_inode(uint32_t inode_id, inode_t* inode)
{
    int offset = inode_id * sizeof(inode_t);
    int sector = FS_START_SECTOR + sb.inode_offset + offset / SECTOR_SIZE;
    int off_in_sector = offset % SECTOR_SIZE;
    uint8_t buf[2*SECTOR_SIZE];
    bios_sd_read(kva2pa(buf), 2, sector);
    memcpy(buf+off_in_sector, inode, sizeof(inode_t*));
    bios_sd_write(kva2pa(buf), 2, sector);
}

uint32_t alloc_inode()
{
    for (int byte = 0; byte < sb.inode_map_sz * SECTOR_SIZE; byte++) 
    {
        if (inode_map[byte] != 0xFF) 
        {
            for (int bit = 0; bit < 8; bit++) 
            {
                if (!(inode_map[byte] & (1 << bit))) 
                {
                    inode_map[byte] |= (1 << bit);
                    bios_sd_write(kva2pa(inode_map), 1, FS_START_SECTOR + sb.inode_map_offset);
                    return byte * 8 + bit;
                }
            }
        }
    }
    return -1;
}

uint32_t alloc_block()
{
    for (int byte = 0; byte < sb.block_map_sz * SECTOR_SIZE; byte++) 
    {
        if (block_map[byte] != 0xFF)
        {
            for (int bit = 0; bit < 8; bit++) 
            {
                if (!(block_map[byte] & (1 << bit)))
                {
                    block_map[byte] |= (1 << bit);
                    int sector_begin = byte / SECTOR_SIZE;
                    bios_sd_write(kva2pa(block_map + sector_begin * SECTOR_SIZE), 1, 
                                  FS_START_SECTOR + sb.block_map_offset + sector_begin);
                    return byte * 8 + bit;
                }
            }
        }
    }
    return -1;
}

int do_mkfs(void)   // create new file system
{
    // TODO [P6-task1]: Implement do_mkfs
    printk("[FS] Start init file system!\n");
    printk("[FS] Setting superblock\n");
    sb.magic = MAGIC_NUM;
    sb.start_sector = FS_START_SECTOR;
    sb.num_sector = FS_START_SECTOR;

    sb.inode_map_offset = 1;
    sb.inode_map_sz = 1;    // 4096 bit=512B

    sb.block_map_offset = sb.inode_map_offset+sb.inode_map_sz;
    sb.block_map_sz = 32;   // 512MB/4KB bit = 16KB = 32 sector

    sb.inode_offset = sb.block_map_offset+sb.block_map_sz;
    sb.inode_sz =  1024; // sizeof inode: 128B; 4096*128B / 512B =  1024

    sb.data_offset = sb.inode_offset+sb.inode_sz;
    sb.data_sz = sb.num_sector-sb.data_offset;

    printk("    magic: 0x%x\n", sb.magic);
    printk("    num sector: %d;    start sector: %d\n", sb.num_sector, sb.start_sector);
    printk("    inode map offset: %d (%d)\n", sb.inode_map_offset, sb.inode_map_sz);
    printk("    block map offset: %d (%d)\n", sb.block_map_offset, sb.block_map_sz);
    printk("    inode offset: %d (%d)\n", sb.inode_offset, sb.inode_sz);
    printk("    data offset: %d (%d)\n", sb.data_offset, sb.data_sz);
    printk("    inode entry size %dB, dir entry size: %dB\n", sizeof(inode_t), sizeof(dentry_t));

    // init block_map and inode_map
    memset(inode_map, 0, sizeof(inode_map));
    memset(block_map, 0, sizeof(block_map));
    inode_map[0] |= 0x03; // 0b0011
    block_map[0] |= 0x01; // Set bit 0 (Block 0)
    
    // write superblock, inode_map, block_map to sd
    bios_sd_write(kva2pa(&sb), 1, FS_START_SECTOR);
    bios_sd_write(kva2pa(inode_map), 1, FS_START_SECTOR + sb.inode_map_offset);

    for (int i = 0; i < sb.block_map_sz; i += SEC_PER_BLOCK)
        bios_sd_write(kva2pa(block_map + i * SECTOR_SIZE), SEC_PER_BLOCK, FS_START_SECTOR + sb.block_map_offset + i);
    
    memset(temp_buf, 0, BLOCK_SIZE);
    for (int i = 0; i < sb.inode_sz; i += SEC_PER_BLOCK)    // clear inode
        bios_sd_write(kva2pa(temp_buf), SEC_PER_BLOCK, FS_START_SECTOR + sb.inode_offset + i);
    
    // set root
    inode_t root;
    root.mode = IM_DIR;
    root.link_count = 2;    // . and ..
    root.size = 2 * sizeof(dentry_t);

    int root_inode_idx = 1;
    int l1_blk_id = alloc_block(); 
    int data_blk_id = alloc_block(); 
    if (l1_blk_id < 0 || data_blk_id < 0) 
    {
        printk("Error: No space for root init!\n");
        return;
    }
    root.indirect_block = l1_blk_id;

    // write root inode
    uint32_t inode_sector = FS_START_SECTOR + sb.inode_offset + 
                        (root_inode_idx * sizeof(inode_t)) / SECTOR_SIZE;
    uint32_t inode_off = (root_inode_idx * sizeof(inode_t)) % SECTOR_SIZE;
    bios_sd_read(kva2pa(temp_buf), 1, inode_sector);
    memcpy(temp_buf + inode_off, &root, sizeof(inode_t));
    bios_sd_write(kva2pa(temp_buf), 1, inode_sector);

    // write l1 block
    memset(temp_buf, 0, BLOCK_SIZE);
    uint32_t *index_ptr = (uint32_t *)temp_buf;
    index_ptr[0] = data_blk_id; // block 0 --> data_blk_id(block 1)
    bios_sd_write(kva2pa(temp_buf), SEC_PER_BLOCK, get_block_sector(l1_blk_id));
    
    memset(temp_buf, 0, BLOCK_SIZE);
    dentry_t *dentries = (dentry_t *)temp_buf;
    strcpy(dentries[0].name, ".");
    dentries[0].inode_id = 1;
    strcpy(dentries[1].name, "..");
    dentries[1].inode_id = 1;
    bios_sd_write(kva2pa(temp_buf), SEC_PER_BLOCK, get_block_sector(data_blk_id));

    return 0;  // do_mkfs succeeds
}


int count_set_bits(uint8_t byte) 
{   // count byte's bit 1 num
    int count = 0;
    for (int i = 0; i < 8; i++) 
    {
        if (byte & (1 << i)) count++;
    }
    return count;
}

int do_statfs(void)
{
    // TODO [P6-task1]: Implement do_statfs
    if (sb.magic != MAGIC_NUM) 
    {
        printk("[Error] Filesystem not found! Magic: 0x%x\n", sb.magic);
        return -1;
    }
    // count used inodes
    int used_inodes = 0;
    for (int i = 0; i < sb.inode_map_sz; i++) 
    {
        bios_sd_read(kva2pa(temp_buf), 1, FS_START_SECTOR + sb.inode_map_offset + i);
        for (int j = 0; j < SECTOR_SIZE; j++)
            used_inodes += count_set_bits(temp_buf[j]);
    }

    // count used data blocks
    int used_blocks = 0;
    for (int i = 0; i < sb.block_map_sz; i++) 
    {
        bios_sd_read(kva2pa(temp_buf), 1, FS_START_SECTOR + sb.block_map_offset + i);
        for (int j = 0; j < SECTOR_SIZE; j++)
            used_blocks += count_set_bits(temp_buf[j]);
    }

    int metadata_sectors = 1 + sb.inode_map_sz + sb.block_map_sz + sb.inode_sz;
    int data_sectors = used_blocks * SEC_PER_BLOCK;
    int total_used_sectors = metadata_sectors + data_sectors;

    printk("magic: 0x%x\n", sb.magic);
    printk("used sector: %d/%d,     start sector: %d\n", 
           total_used_sectors, sb.num_sector, sb.start_sector);
    int total_inodes = sb.inode_map_sz * SECTOR_SIZE * 8;
    printk("inode map offset %d, occupied sector %d, used: %d/%d\n", 
           sb.inode_map_offset, sb.inode_map_sz, used_inodes, total_inodes);
    printk("block map offset %d,    occupied sector: %d\n", 
           sb.block_map_offset, sb.block_map_sz);
    // Inode Table
    printk("inode offset %d, occupied sector: %d\n", 
           sb.inode_offset, sb.inode_sz);
    // Data
    printk("data offset %d, occupied sector: %d\n", 
           sb.data_offset, sb.data_sz); 
    // Entry Size
    printk("inode entry size %dB, dir entry size: %dB\n", 
           sizeof(inode_t), sizeof(dentry_t));
    return 0;  // do_statfs succeeds
}

int do_cd(char *path)
{
    // TODO [P6-task1]: Implement do_cd
    return 0;  // do_cd succeeds
}

int lookup_entry(inode_t* parnet_inode, const char* path)
{
    if(parnet_inode->mode!=IM_DIR)  return -1;  // not entry
    uint32_t l1_blk_id = parnet_inode->indirect_block;
    // check data_blk to find if path exists
}
int do_mkdir(char *path)
{
    // TODO [P6-task1]: Implement do_mkdir
    inode_t parent_inode;
    read_inode(current_dir_inode_id, &parent_inode);
    if(lookup_entry(&parent_inode, path))    return -1;  // entry existed

    uint32_t new_inode_id = alloc_inode();
    uint32_t l1_block_id = alloc_block();   
    uint32_t data_block_id = alloc_block();

    if (new_inode_id<=0 || l1_block_id<=0 || data_block_id<=0) 
    {
        printk("Error: No space for mkdir\n");
        return -1;
    }

    inode_t new_inode;
    new_inode.mode = IM_DIR;
    new_inode.link_count = 2;
    new_inode.size = 2*sizeof(dentry_t);
    new_inode.indirect_block = l1_block_id;
    write_inode(new_inode_id, &new_inode);

    memset(temp_buf, 0, BLOCK_SIZE);
    uint32_t *idx_ptr = (uint32_t *)temp_buf;
    idx_ptr[0] = data_block_id;
    bios_sd_write(kva2pa(temp_buf), SEC_PER_BLOCK, get_block_sector(l1_block_id));

    // 5.2 写入数据块内容
    memset(temp_buf, 0, BLOCK_SIZE);
    dentry_t *dentries = (dentry_t *)temp_buf;

    // dentry[0]: "." 指向自己 [cite: 213]
    strcpy(dentries[0].name, ".");
    dentries[0].inode_id = new_inode_id;
    dentries[0].valid = 1; // 假设 dentry 结构体有个 valid 位

    // dentry[1]: ".." 指向父目录 [cite: 213]
    strcpy(dentries[1].name, "..");
    dentries[1].inode_id = parent_inode_id; // 这里的父目录就是 current_dir
    dentries[1].valid = 1;

    // 写入数据块
    bios_sd_write(kva2pa(temp_buf), SEC_PER_BLOCK, get_block_sector(data_block_id));

    // ==========================================
    // 步骤 6: 更新父目录 (添加新 dentry) [cite: 217-218]
    // ==========================================
    // 这步最关键！需要在父目录的数据块里找到一个空位置，把 new_dir_name 写进去
    
    // 1. 获取父目录的第 0 个逻辑块 (通过父目录 Inode 的 indirect_block -> 索引块 -> 数据块)
    // 简化：假设父目录还没满，只读第 0 块
    uint32_t parent_l1_id = parent_inode.indirect_block;
    bios_sd_read(kva2pa(temp_buf), SEC_PER_BLOCK, get_block_sector(parent_l1_id));
    uint32_t parent_data_id = ((uint32_t*)temp_buf)[0]; // 获取父目录数据块 ID

    // 2. 读取父目录数据块
    bios_sd_read(kva2pa(temp_buf), SEC_PER_BLOCK, get_block_sector(parent_data_id));
    dentries = (dentry_t *)temp_buf;

    // 3. 寻找空位
    int found_slot = 0;
    int max_dentry = BLOCK_SIZE / sizeof(dentry_t);
    for (int i = 0; i < max_dentry; i++) {
        if (dentries[i].inode_id == 0) { // 找到空位 (假设 id=0 表示无效)
            strcpy(dentries[i].name, new_dir_name);
            dentries[i].inode_id = new_inode_id;
            // dentries[i].type = IM_DIR; // 如果 dentry 也有 type 字段
            found_slot = 1;
            break;
        }
    }

    if (!found_slot) {
        printk("Error: Parent directory full!\n");
        // 实际上应该分配新的数据块给父目录，但这属于进阶实现
        return -1;
    }

    // 4. 写回父目录数据块
    bios_sd_write(kva2pa(temp_buf), SEC_PER_BLOCK, get_block_sector(parent_data_id));

    // ==========================================
    // 步骤 7: 更新父目录 Inode (Size, Link Count) [cite: 222]
    // ==========================================
    // 父目录大小增加了 (多了一个 dentry)
    parent_inode.size += sizeof(dentry_t); 
    // 父目录的链接数增加了 (因为子目录里有一个 .. 指向它)
    // 这是一个经典的文件系统特性：目录的硬链接数 = 2 + 子目录数量
    parent_inode.link_count++; 

    // 写回父目录 Inode
    // write_inode(parent_inode_id, &parent_inode);

    return 0;  // do_mkdir succeeds
}

int do_rmdir(char *path)
{
    // TODO [P6-task1]: Implement do_rmdir

    return 0;  // do_rmdir succeeds
}

int do_ls(char *path, int option)
{
    // TODO [P6-task1]: Implement do_ls
    // Note: argument 'option' serves for 'ls -l' in A-core

    return 0;  // do_ls succeeds
}

int do_open(char *path, int mode)
{
    // TODO [P6-task2]: Implement do_open

    return 0;  // return the id of file descriptor
}

int do_read(int fd, char *buff, int length)
{
    // TODO [P6-task2]: Implement do_read

    return 0;  // return the length of trully read data
}

int do_write(int fd, char *buff, int length)
{
    // TODO [P6-task2]: Implement do_write

    return 0;  // return the length of trully written data
}

int do_close(int fd)
{
    // TODO [P6-task2]: Implement do_close

    return 0;  // do_close succeeds
}

int do_ln(char *src_path, char *dst_path)
{
    // TODO [P6-task2]: Implement do_ln

    return 0;  // do_ln succeeds 
}

int do_rm(char *path)
{
    // TODO [P6-task2]: Implement do_rm

    return 0;  // do_rm succeeds 
}

int do_lseek(int fd, int offset, int whence)
{
    // TODO [P6-task2]: Implement do_lseek

    return 0;  // the resulting offset location from the beginning of the file
}
