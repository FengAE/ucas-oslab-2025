#include <os/string.h>
#include <os/fs.h>
#include <printk.h>
#include <os/lock.h>

static fdesc_t fdesc_array[NUM_FDESCS];
static superblock_t sb; // global superblock
static spin_lock_t fs_lock;

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
    bios_sd_read(kva2pa((uintptr_t)buf), 1, sector);
    memcpy((uint8_t *)inode, buf + off_in_sector, sizeof(inode_t));
}

void write_inode(uint32_t inode_id, inode_t* inode)
{
    int offset = inode_id * sizeof(inode_t);
    int sector = FS_START_SECTOR + sb.inode_offset + offset / SECTOR_SIZE;
    int off_in_sector = offset % SECTOR_SIZE;
    uint8_t buf[2*SECTOR_SIZE];
    bios_sd_read(kva2pa((uintptr_t)buf), 2, sector);
    memcpy(buf+off_in_sector, (uint8_t *)inode, sizeof(inode_t));
    bios_sd_write(kva2pa((uintptr_t)buf), 2, sector);
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
                    inode_map[byte] |= (1 << bit);  // set to 1
                    bios_sd_write(kva2pa((uintptr_t)inode_map), 1, FS_START_SECTOR + sb.inode_map_offset);
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
                    bios_sd_write(kva2pa((uintptr_t)(block_map + sector_begin * SECTOR_SIZE)), 1, 
                                  FS_START_SECTOR + sb.block_map_offset + sector_begin);
                    return byte * 8 + bit;
                }
            }
        }
    }
    return -1;
}

int alloc_dentry(dentry_t* dentries)
{
    int max_dentry = BLOCK_SIZE / sizeof(dentry_t);
    for (int i = 0; i < max_dentry; i++) 
    {
        if (dentries[i].inode_id <= 0)
            return i;
    }
    return -1;
}

int lookup_entry(inode_t* parent_inode, char* path)
{
    if(parent_inode->mode!=IM_DIR)  return -1;  // not entry
    uint32_t data_blk_id = inode_to_block_id(parent_inode);
    bios_sd_read(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(data_blk_id));
    dentry_t *dentries = (dentry_t *)temp_buf;

    if(path==NULL || path[0]=='/' || path[0]=='\0')
        strcpy(path, ".");

    int max_dentry = BLOCK_SIZE / sizeof(dentry_t);
    for (int i = 0; i < max_dentry; i++) 
    {
        if (dentries[i].inode_id > 0 && strcmp(dentries[i].name, path) == 0)
            return dentries[i].inode_id;
    }
    return -1;
}


uint32_t inode_to_block_id(inode_t* inode)
{
    uint32_t l1_id = inode->indirect_block;
    // Use local buffer to avoid corrupting global temp_buf used by caller
    uint8_t local_buf[SECTOR_SIZE]; 
    bios_sd_read(kva2pa((uintptr_t)local_buf), 1, get_block_sector(l1_id)); 
    uint32_t data_id = ((uint32_t*)local_buf)[0];
    return data_id;
}

int do_mkfs(void)   // create new file system
{
    // TODO [P6-task1]: Implement do_mkfs
    spin_lock_acquire(&fs_lock);
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
    bios_sd_write(kva2pa((uintptr_t)&sb), 1, FS_START_SECTOR);
    bios_sd_write(kva2pa((uintptr_t)inode_map), 1, FS_START_SECTOR + sb.inode_map_offset);

    for (int i = 0; i < sb.block_map_sz; i += SEC_PER_BLOCK)
        bios_sd_write(kva2pa((uintptr_t)(block_map + i * SECTOR_SIZE)), 
                    SEC_PER_BLOCK, FS_START_SECTOR + sb.block_map_offset + i);
    
    memset(temp_buf, 0, BLOCK_SIZE);
    for (int i = 0; i < sb.inode_sz; i += SEC_PER_BLOCK)    // clear inode
        bios_sd_write(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, FS_START_SECTOR + sb.inode_offset + i);
    
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
        return -1;
    }
    root.indirect_block = l1_blk_id;

    // write root inode
    uint32_t inode_sector = FS_START_SECTOR + sb.inode_offset + 
                        (root_inode_idx * sizeof(inode_t)) / SECTOR_SIZE;
    uint32_t inode_off = (root_inode_idx * sizeof(inode_t)) % SECTOR_SIZE;
    bios_sd_read(kva2pa((uintptr_t)temp_buf), 1, inode_sector);
    memcpy(temp_buf + inode_off, (uint8_t *)&root, sizeof(inode_t));
    bios_sd_write(kva2pa((uintptr_t)temp_buf), 1, inode_sector);

    // write l1 block
    memset(temp_buf, 0, BLOCK_SIZE);
    uint32_t *index_ptr = (uint32_t *)temp_buf;
    index_ptr[0] = data_blk_id; // block 0 --> data_blk_id(block 1)
    bios_sd_write(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(l1_blk_id));
    
    memset(temp_buf, 0, BLOCK_SIZE);
    dentry_t *dentries = (dentry_t *)temp_buf;
    strcpy(dentries[0].name, ".");
    dentries[0].inode_id = 1;
    strcpy(dentries[1].name, "..");
    dentries[1].inode_id = 1;
    bios_sd_write(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(data_blk_id));

    spin_lock_release(&fs_lock);
    return 0;  // do_mkfs succeeds
}

void fs_init()
{
    bios_sd_read(kva2pa((uintptr_t)&sb), 1, FS_START_SECTOR);
    if(sb.magic != MAGIC_NUM)
    {
        printk("[FS] No valid FS found (Magic: 0x%x). Formatting...\n", sb.magic);
        do_mkfs(); 
    }
    else
    {
        printk("[FS] Found valid FS (Magic: 0x%x). Restoring...\n", sb.magic);
        bios_sd_read(kva2pa((uintptr_t)inode_map), 1, FS_START_SECTOR + sb.inode_map_offset);
        // restore all data back to memory
        for (int i = 0; i < sb.block_map_sz; i += SEC_PER_BLOCK) 
        {
            bios_sd_read(kva2pa((uintptr_t)(block_map + i * SECTOR_SIZE)), 
                         SEC_PER_BLOCK, 
                         FS_START_SECTOR + sb.block_map_offset + i);
        }
        printk("[FS] File system restored.\n");
    }
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
        bios_sd_read(kva2pa((uintptr_t)temp_buf), 1, FS_START_SECTOR + sb.inode_map_offset + i);
        for (int j = 0; j < SECTOR_SIZE; j++)
            used_inodes += count_set_bits(temp_buf[j]);
    }

    // count used data blocks
    int used_blocks = 0;
    for (int i = 0; i < sb.block_map_sz; i++) 
    {
        bios_sd_read(kva2pa((uintptr_t)temp_buf), 1, FS_START_SECTOR + sb.block_map_offset + i);
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
{   // shell ensure path not NULL, and is path/path
    // TODO [P6-task1]: Implement do_cd
    spin_lock_acquire(&fs_lock);

    // 1. Get CWD Inode
    inode_t cwd;
    uint32_t cwd_id = current_running[get_current_cpu_id()]->cwd_inode_id;
    read_inode(cwd_id, &cwd);

    // 2. Lookup target
    char buffer[32];
    int cur = 0, idx = 0, id;
    for(; path[cur]!='\0'; cur++)
    {
        if(path[cur] == '/')
        {
            buffer[idx] = '\0';
            idx = 0;
            id = lookup_entry(&cwd, buffer);
            if (id <= 0) 
            {
                printk("Error: Path not found\n");
                spin_lock_release(&fs_lock);
                return -1;
            }
            read_inode(id, &cwd);
        }
        else
            buffer[idx++] = path[cur];
    }
    // find last level dir
    buffer[idx] = '\0';
    int target_id = lookup_entry(&cwd, buffer);
    if (target_id <= 0) 
    {
        printk("Error: Directory not found\n");
        spin_lock_release(&fs_lock);
        return -1;
    }

    // 3. Verify it is a directory
    inode_t target;
    read_inode(target_id, &target);
    if (target.mode != IM_DIR) {
        printk("Error: %s Not a directory\n", path);
        spin_lock_release(&fs_lock);
        return -1;
    }

    // 4. Update PCB
    current_running[get_current_cpu_id()]->cwd_inode_id = target_id;
    spin_lock_release(&fs_lock);
    return 0;  // do_cd succeeds
}

int do_mkdir(char *path)
{
    // TODO [P6-task1]: Implement do_mkdir
    spin_lock_acquire(&fs_lock);
    inode_t parent_inode;
    read_inode(current_running[get_current_cpu_id()]->cwd_inode_id, &parent_inode);
    if(lookup_entry(&parent_inode, path) > 0)    
    {
        printk("Error: Directory %s exists!\n", path);
        spin_lock_release(&fs_lock);
        return -1;
    }

    uint32_t new_inode_id = alloc_inode();
    uint32_t l1_block_id = alloc_block();   
    uint32_t data_block_id = alloc_block();

    if (new_inode_id<0 || l1_block_id<0 || data_block_id<0) 
    {
        printk("Error: No space for mkdir\n");
        spin_lock_release(&fs_lock);
        return -1;
    }
    printl("begin to mkdir %s--> l1_block_id: %d, data_block_id: %d\n", path, l1_block_id, data_block_id);

    // 1. init new inode
    inode_t new_inode;
    new_inode.mode = IM_DIR;
    new_inode.link_count = 2;
    new_inode.size = 2*sizeof(dentry_t);
    new_inode.indirect_block = l1_block_id;
    write_inode(new_inode_id, &new_inode);

    memset(temp_buf, 0, BLOCK_SIZE);
    uint32_t *idx_ptr = (uint32_t *)temp_buf;
    idx_ptr[0] = data_block_id;
    bios_sd_write(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(l1_block_id));

    // 2. write new data block and l1 block
    memset(temp_buf, 0, BLOCK_SIZE);
    dentry_t *dentries = (dentry_t *)temp_buf;
    strcpy(dentries[0].name, ".");
    dentries[0].inode_id = new_inode_id;
    strcpy(dentries[1].name, "..");
    dentries[1].inode_id = current_running[get_current_cpu_id()]->cwd_inode_id;

    bios_sd_write(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(data_block_id));


    // 3. refresh parent data block: add new dentry
    uint32_t parent_data_id = inode_to_block_id(&parent_inode);
    bios_sd_read(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(parent_data_id));
    dentries = (dentry_t *)temp_buf;

    // find empty dentry pos to add
    int dentry_idx = alloc_dentry(dentries);
    if (dentry_idx < 0) 
    {   // should alloc new data block to parent dir?
        printk("Error: Parent directory full!\n");
        spin_lock_release(&fs_lock);
        return -1;
    }
    strcpy(dentries[dentry_idx].name, path);
    dentries[dentry_idx].inode_id = new_inode_id;
    // write back new parent data block
    bios_sd_write(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(parent_data_id));

    
    // 4. refresh parent dir's inode
    parent_inode.size += sizeof(dentry_t); 
    parent_inode.link_count++; 
    write_inode(current_running[get_current_cpu_id()]->cwd_inode_id, &parent_inode);

    spin_lock_release(&fs_lock);
    return 0;  // do_mkdir succeeds
}


int is_dir_empty(inode_t *dir_inode) 
{   // assume dir is single level
    uint32_t data_block_id = inode_to_block_id(dir_inode);
    bios_sd_read(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(data_block_id));
    dentry_t *dentries = (dentry_t *)temp_buf;
    int max_dentry = BLOCK_SIZE / sizeof(dentry_t);
    
    for (int i = 0; i < max_dentry; i++) 
    {
        if (dentries[i].inode_id > 0) {
            char *name = dentries[i].name;
            if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
                return 0; // Found a file/dir other than . and ..
        }
    }
    return 1;
}

int do_rmdir(char *path)
{
    // TODO [P6-task1]: Implement do_rmdir
    spin_lock_acquire(&fs_lock);
    
    // 1. Read Parent Inode
    inode_t parent_inode;
    uint32_t parent_id = current_running[get_current_cpu_id()]->cwd_inode_id;
    read_inode(parent_id, &parent_inode);

    // 2. Find target dentry in Parent
    // We need the index to clear it later, lookup_entry only returns ID.
    // So we repeat lookup logic here
    uint32_t parent_data_id = inode_to_block_id(&parent_inode);
    bios_sd_read(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(parent_data_id));
    dentry_t *dentries = (dentry_t *)temp_buf;
    
    int target_idx = -1;
    uint32_t target_inode_id = 0;
    int max_dentry = BLOCK_SIZE / sizeof(dentry_t);

    for (int i = 0; i < max_dentry; i++) 
    {
        if (dentries[i].inode_id > 0 && strcmp(dentries[i].name, path) == 0) 
        {
            target_idx = i;
            target_inode_id = dentries[i].inode_id;
            break;
        }
    }
    if (target_idx == -1) 
    {
        printk("Error: Directory not found\n");
        spin_lock_release(&fs_lock);
        return -1;
    }

    // 3. Read Target Inode to check if it is a directory and empty
    inode_t target_inode;
    read_inode(target_inode_id, &target_inode);

    if (target_inode.mode != IM_DIR) 
    {
        printk("Error: Not a directory\n");
        spin_lock_release(&fs_lock);
        return -1;
    }
    if (!is_dir_empty(&target_inode)) 
    {
        printk("Error: Directory not empty\n");
        spin_lock_release(&fs_lock);
        return -1;
    }

    // 4. Free Resources (Inode, Blocks)
    inode_map[target_inode_id/8] &= ~(1<<(target_inode_id % 8));
    bios_sd_write(kva2pa((uintptr_t)inode_map), sb.inode_map_sz, FS_START_SECTOR + sb.inode_map_offset);

    uint32_t l1_block_id = target_inode.indirect_block;
    block_map[l1_block_id/8] &= ~(1<<(l1_block_id % 8));
    bios_sd_read(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(l1_block_id));
    uint32_t data_block_id = temp_buf[0];
    block_map[data_block_id/8] &= ~(1<<(data_block_id % 8));
    bios_sd_write(kva2pa((uintptr_t)block_map), sb.block_map_sz, FS_START_SECTOR + sb.block_map_offset);

    printl("rmdir %s--> l1_block_id: %d, data_block_id: %d\n", path, l1_block_id, data_block_id);
    // 5. Remove dentry from Parent
    // Re-read parent data because is_dir_empty messed up temp_buf
    bios_sd_read(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(parent_data_id));
    dentries = (dentry_t *)temp_buf;
    
    dentries[target_idx].inode_id = 0; // Mark as free
    dentries[target_idx].name[0] = '\0';
    
    bios_sd_write(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(parent_data_id));

    // 6. Update Parent Inode
    parent_inode.link_count--;
    parent_inode.size -= sizeof(dentry_t); // Optional
    write_inode(parent_id, &parent_inode);

    spin_lock_release(&fs_lock);
    return 0;
}

int do_ls(char *path, int option)
{   // shell ensure path is like path/path
    // TODO [P6-task1]: Implement do_ls
    // Note: argument 'option' serves for 'ls -l' in A-core
    spin_lock_acquire(&fs_lock);

    // 1. Determine which dir to list, support multi level
    inode_t cwd, dir_inode;
    if (path == NULL || path[0] == '\0')
        read_inode(current_running[get_current_cpu_id()]->cwd_inode_id, &dir_inode);
    else
    {
        read_inode(current_running[get_current_cpu_id()]->cwd_inode_id, &cwd);
        char buffer[32];
        int cur = 0, idx = 0, id;
        for(; path[cur]!='\0'; cur++)
        {
            if(path[cur] == '/')
            {
                buffer[idx] = '\0';
                idx = 0;
                id = lookup_entry(&cwd, buffer);
                if (id <= 0) 
                {
                    printk("Error: Path %s not found\n", path);
                    spin_lock_release(&fs_lock);
                    return -1;
                }
                read_inode(id, &cwd);
            }
            else
                buffer[idx++] = path[cur];
        }
        // find last level dir
        buffer[idx] == '\0';
        id = lookup_entry(&cwd, buffer);
        if (id <= 0) 
        {
            printk("Error: Path %s not found\n", path);
            spin_lock_release(&fs_lock);
            return -1;
        }
        read_inode(id, &dir_inode);
    }

    // 2. Read entries
    if(dir_inode.mode != IM_DIR)
    {
        printk("Path %s is not a directory\n", path);
        spin_lock_release(&fs_lock);
        return -1;
    }

    uint32_t data_id = inode_to_block_id(&dir_inode);
    bios_sd_read(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(data_id));
    dentry_t *dentries = (dentry_t *)temp_buf;

    int max_dentry = BLOCK_SIZE / sizeof(dentry_t);
    for (int i = 0; i < max_dentry; i++) 
    {
        if (dentries[i].inode_id > 0) 
        {
            if (option == 1) 
            { // ls -l or ll
                inode_t file_inode;
                read_inode(dentries[i].inode_id, &file_inode);
                printk("%s  \tinode: %d, links: %d, size: %d\n", 
                       dentries[i].name, dentries[i].inode_id, file_inode.link_count, file_inode.size);
            } 
            else
                printk("%s ", dentries[i].name);
        }
    }
    printk("\n");

    spin_lock_release(&fs_lock);
    return 0;
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
