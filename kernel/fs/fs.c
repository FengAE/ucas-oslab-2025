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


void free_inode(int inode_id)
{
    if(inode_id <= 0)   return;
    int byte_offset = inode_id / 8;
    inode_map[byte_offset] &= ~(1<<(inode_id % 8));  // clear inode_map and inode
    uint32_t map_sector_offset = byte_offset / SECTOR_SIZE;
    uint32_t sd_sector = FS_START_SECTOR + sb.inode_map_offset + map_sector_offset;
    
    uintptr_t mem_addr = (uintptr_t)inode_map + map_sector_offset * SECTOR_SIZE;
    bios_sd_write(kva2pa(mem_addr), 1, sd_sector);
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

void free_block(uint32_t block_id)
{
    if (block_id <= 0) return;
    int byte_offset = block_id / 8;
    int bit_offset = block_id % 8;
    // Clear bit in global memory map
    block_map[byte_offset] &= ~(1 << bit_offset);

    // Sync specific sector of block map to SD card
    int sector_begin = byte_offset / SECTOR_SIZE;
    bios_sd_write(kva2pa((uintptr_t)(block_map + sector_begin * SECTOR_SIZE)), 
                  1, 
                  FS_START_SECTOR + sb.block_map_offset + sector_begin);
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

int lookup_entry_single(inode_t* parent_inode, char* path)
{   // only check cur dir!
    if(parent_inode->mode!=IM_DIR)  return -1;  // not entry
    uint32_t data_blk_id = inode_to_block_id(parent_inode);
    bios_sd_read(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(data_blk_id));
    dentry_t *dentries = (dentry_t *)temp_buf;  // in prj: set . as 0, and .. as 1

    char path_buf[32];
    if(path==NULL || path[0]=='/' || path[0]=='\0' ||
        (dentries[0].inode_id==1 && strcmp(path, "..")==0)) // is root and cd ..
        strcpy(path_buf, ".");
    else
        strcpy(path_buf, path);

    int max_dentry = BLOCK_SIZE / sizeof(dentry_t);
    for (int i = 0; i < max_dentry; i++) 
    {
        if (dentries[i].inode_id > 0 && strcmp(dentries[i].name, path_buf) == 0)
            return dentries[i].inode_id;    // if is 0: not valid!
    }
    return -1;
}

int lookup_entry(inode_t cwd, char* path)
{   // path may be multiple
    char buffer[32];
    int cur = 0, idx = 0, id;
    for(; path[cur]!='\0'; cur++)
    {
        if(path[cur] == '/')
        {
            buffer[idx] = '\0';
            idx = 0;
            id = lookup_entry_single(&cwd, buffer);
            if (id <= 0)    return -1;
            read_inode(id, &cwd);
        }
        else
            buffer[idx++] = path[cur];
    }
    // find last level dir
    buffer[idx] = '\0';
    int target_id = lookup_entry_single(&cwd, buffer);
    if (target_id <= 0)    return -1;
    return target_id;
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
    int l2_blk_id = alloc_block();
    int data_blk_id = alloc_block(); 
    if (l1_blk_id < 0 || l2_blk_id < 0 || data_blk_id < 0) 
    {
        printk("Error: No space for root init!\n");
        return -1;
    }
    root.indirect_block = l1_blk_id;
    root.double_indirect_block = l2_blk_id;

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
    index_ptr[0] = data_blk_id; // l1 block --> data block
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
    int target_id = lookup_entry(cwd, path);
    if (target_id < 0) 
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
    uint32_t parent_id = current_running[get_current_cpu_id()]->cwd_inode_id;
    read_inode(parent_id, &parent_inode);
    if(lookup_entry_single(&parent_inode, path) > 0)    
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
    memset(&new_inode, 0, sizeof(new_inode));   // clear to ensure not interrupt by old data
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
    dentries[1].inode_id = parent_id;

    bios_sd_write(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(data_block_id));

    // 3. refresh parent data block: add new dentry
    parent_add_dentry(&parent_inode, parent_id, path, new_inode_id, 0);

    spin_lock_release(&fs_lock);
    return 0;  // do_mkdir succeeds
}

int parent_add_dentry(inode_t* parent_inode, uint32_t parent_id, char* name, int new_inode_id, int mode)
{   // Add specific name and inode_id file or dir, into parent_inode
    // mode=1: file; mode=0: dir
    uint32_t parent_data_id = inode_to_block_id(parent_inode);
    bios_sd_read(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(parent_data_id));
    dentry_t* dentries = (dentry_t *)temp_buf;

    // find empty dentry pos to add
    int dentry_idx = alloc_dentry(dentries);
    if (dentry_idx < 0) 
    {   // should alloc new data block to parent dir?
        printk("Error: Parent directory full!\n");
        return -1;
    }
    strcpy(dentries[dentry_idx].name, name);
    dentries[dentry_idx].inode_id = new_inode_id;
    // write back new parent data block
    bios_sd_write(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(parent_data_id));

    parent_inode->size += sizeof(dentry_t);
    if(mode == 0)   parent_inode->link_count++;
    write_inode(parent_id, parent_inode);
    return 0;
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
    // We need the index to clear it later, lookup_entry_single only returns ID.
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
    free_inode(target_inode_id);

    uint32_t l1_block_id = target_inode.indirect_block;
    free_block(l1_block_id);
    bios_sd_read(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, get_block_sector(l1_block_id));
    uint32_t data_block_id = temp_buf[0];
    free_block(data_block_id);
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
        int id = lookup_entry(cwd, path);
        if (id < 0) 
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

uint32_t fs_get_block_sector(uint32_t inode_id, uint32_t logical_block, int create) 
{   // create: if block not exists, whether alloc new block
    // create=0: read(); create=1: write()
    inode_t inode;
    read_inode(inode_id, &inode);
    uint32_t tmp_buf[BLOCK_SIZE / sizeof(uint32_t)];    // store block ptr
    uint32_t phy_block_id = 0;

    // --- Case 1: indirect_block (0 ~ 4MB) ---
    if (logical_block < 1024) 
    {
        if (inode.indirect_block == 0) // not written l1 blk yet
        {
            if (!create) return 0; // null hole
            inode.indirect_block = alloc_block();
            // clear indirect block in sd card first
            memset(tmp_buf, 0, BLOCK_SIZE);
            bios_sd_write(kva2pa((uintptr_t)tmp_buf), SEC_PER_BLOCK, get_block_sector(inode.indirect_block));
            write_inode(inode_id, &inode);
        }
        
        // alloc physical data block
        bios_sd_read(kva2pa((uintptr_t)tmp_buf), SEC_PER_BLOCK, get_block_sector(inode.indirect_block));
        phy_block_id = tmp_buf[logical_block];
        if (phy_block_id == 0) 
        {
            if (!create) return 0;
            phy_block_id = alloc_block();
            tmp_buf[logical_block] = phy_block_id;
            // write back l1 block
            bios_sd_write(kva2pa((uintptr_t)tmp_buf), SEC_PER_BLOCK, get_block_sector(inode.indirect_block));
        }
        return get_block_sector(phy_block_id);
    }
    
    // --- Case 2: doulbe_indirect_block (4MB+) ---
    else   // l2 --> l1 --> data
    {
        logical_block -= 1024;
        uint32_t l1_idx = logical_block / 1024;
        uint32_t data_idx = logical_block % 1024;
        // 1. check l2 block
        if (inode.double_indirect_block == 0) 
        {
            if (!create) return 0;
            inode.double_indirect_block = alloc_block();
            // clear l2
            memset(tmp_buf, 0, BLOCK_SIZE);
            bios_sd_write(kva2pa((uintptr_t)tmp_buf), SEC_PER_BLOCK, get_block_sector(inode.double_indirect_block));
            write_inode(inode_id, &inode);
        }

        // 2. read and check l1 blk
        bios_sd_read(kva2pa((uintptr_t)tmp_buf), SEC_PER_BLOCK, get_block_sector(inode.double_indirect_block));
        uint32_t l1_phy_id = tmp_buf[l1_idx];
        if (l1_phy_id == 0) 
        {
            if (!create) return 0;
            l1_phy_id = alloc_block();
            tmp_buf[l1_idx] = l1_phy_id;
            bios_sd_write(kva2pa((uintptr_t)tmp_buf), SEC_PER_BLOCK, get_block_sector(inode.double_indirect_block));
            // clear l1 blk dirty data
            memset(tmp_buf, 0, BLOCK_SIZE);
            bios_sd_write(kva2pa((uintptr_t)tmp_buf), SEC_PER_BLOCK, get_block_sector(l1_phy_id));
        }

        // 3. read and check data blk
        bios_sd_read(kva2pa((uintptr_t)tmp_buf), SEC_PER_BLOCK, get_block_sector(l1_phy_id));
        phy_block_id = tmp_buf[data_idx];
        if (phy_block_id == 0) 
        {
            if (!create) return 0;
            phy_block_id = alloc_block();
            tmp_buf[data_idx] = phy_block_id;
            bios_sd_write(kva2pa((uintptr_t)tmp_buf), SEC_PER_BLOCK, get_block_sector(l1_phy_id));
        }
        return get_block_sector(phy_block_id);
    }
}

int do_open(char *path, int mode)
{   // assume path is simple name, not path/name
    // TODO [P6-task2]: Implement do_open
    spin_lock_acquire(&fs_lock);

    inode_t cwd, target_inode;
    memset(&target_inode, 0, sizeof(target_inode));
    uint32_t cwd_id = current_running[get_current_cpu_id()]->cwd_inode_id;
    read_inode(cwd_id, &cwd);
    int inode_id = lookup_entry(cwd, path);

    if (inode_id < 0) 
    {
        if (mode == O_RDONLY) 
        {
            printk("File not found: %s\n", path);
            spin_lock_release(&fs_lock);
            return -1;
        }
        
        // create new file
        inode_id = alloc_inode();
        if (inode_id < 0) 
        {
            spin_lock_release(&fs_lock); 
            return -1;
        }
        target_inode.mode = IM_FILE;
        target_inode.link_count = 1;
        target_inode.size = 0;
        target_inode.indirect_block = 0;    // Alloc when do_write
        target_inode.double_indirect_block = 0;
        write_inode(inode_id, &target_inode);

        // Add to parent's dentry
        int ret = parent_add_dentry(&cwd, cwd_id, path, inode_id, 1);
        if(ret < 0)
        {
            free_inode(inode_id);
            spin_lock_release(&fs_lock);
            return -1;
        }
    }

    // 3. distribute fd
    int fd = -1;
    for (int i = 0; i < NUM_FDESCS; i++) 
    {
        if (fdesc_array[i].inode_id == 0) 
        {
            fd = i;
            break;
        }
    }
    if (fd == -1) 
    {
        printk("Error: No free fd\n");
        spin_lock_release(&fs_lock);
        return -1;
    }
    fdesc_array[fd].inode_id = inode_id;
    fdesc_array[fd].access = mode;
    fdesc_array[fd].pos = 0;
    spin_lock_release(&fs_lock);
    return fd;
}

int do_read(int fd, char *buff, int length)
{
    // TODO [P6-task2]: Implement do_read
    spin_lock_acquire(&fs_lock);
    // 1. Check fd
    if (fd < 0 || fd >= NUM_FDESCS || fdesc_array[fd].inode_id == 0) 
    {
        spin_lock_release(&fs_lock);
        return -1;
    }
    if (fdesc_array[fd].access == O_WRONLY) 
    {
        spin_lock_release(&fs_lock);
        return -1;
    }

    fdesc_t *f = &fdesc_array[fd];
    inode_t inode;
    read_inode(f->inode_id, &inode);
    // Cap length to EOF
    printl("read pos: %d\n", f->pos);
    if (f->pos >= inode.size) 
    {
        spin_lock_release(&fs_lock);
        return 0; // End of file
    }
    if (length > inode.size - f->pos)
        length = inode.size - f->pos;

    int read_count = 0;
    while (read_count < length) 
    {
        uint32_t logic_blk_idx = f->pos / BLOCK_SIZE;
        uint32_t offset_in_blk = f->pos % BLOCK_SIZE;
        uint32_t len_in_blk = BLOCK_SIZE - offset_in_blk;
        
        if (len_in_blk > (length - read_count)) 
            len_in_blk = length - read_count;
        // Get physical sector (create=0, do not allocate if missing)
        uint32_t sector = fs_get_block_sector(f->inode_id, logic_blk_idx, 0);
        printl("read sector: %d\n", sector);

        if (sector == 0)
            // HOLE: Logical block exists but no physical block allocated
            // Fill user buffer with zeros
            memset(buff + read_count, 0, len_in_blk);
        else 
        {
            // If we are not reading a full block, read to temp_buf first
            // Note: fs_get_block_sector returns the start sector address of the 4KB block
            // optimization: if block aligned and full block read, could read directly (omitted for safety)
            bios_sd_read(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, sector);
            memcpy(buff + read_count, temp_buf + offset_in_blk, len_in_blk);
            for(int i=0; i<len_in_blk; i++)
            {
                printl("%c", (char*)(buff+read_count)[i]);
            }
            printl("\n");
        }
        f->pos += len_in_blk;
        read_count += len_in_blk;
    }
    spin_lock_release(&fs_lock);
    return read_count;
}

int do_write(int fd, char *buff, int length)
{
    // TODO [P6-task2]: Implement do_write
    spin_lock_acquire(&fs_lock);

    fdesc_t *f = &fdesc_array[fd];
    if (fd < 0 || fd >= NUM_FDESCS || f->inode_id <= 0 
        || f->access == O_RDONLY) // not open or not write enable
    {
        spin_lock_release(&fs_lock); 
        return -1;
    }
    inode_t inode;
    read_inode(f->inode_id, &inode);
    int written = 0;
    while (written < length) 
    {
        uint32_t logic_blk_idx = f->pos / BLOCK_SIZE;
        uint32_t offset_in_blk = f->pos % BLOCK_SIZE;
        uint32_t len_in_blk = BLOCK_SIZE - offset_in_blk;
        if (len_in_blk > (length - written)) 
            len_in_blk = length - written;

        // get physical sector
        uint32_t sector = fs_get_block_sector(f->inode_id, logic_blk_idx, 1);   // alloc l1 and l2 block, write to sd card
        
        // Read-Modify-Write
        if (len_in_blk < BLOCK_SIZE) // not cover whole block
            bios_sd_read(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, sector);
        memcpy(temp_buf + offset_in_blk, buff + written, len_in_blk);
        bios_sd_write(kva2pa((uintptr_t)temp_buf), SEC_PER_BLOCK, sector);
        printl("write sector: %d\n", sector);
        for(int i=0; i<len_in_blk; i++)
            printl("%c", (char*)(temp_buf+offset_in_blk)[i]);
        printl("\n");

        f->pos += len_in_blk;
        written += len_in_blk;
    }
    // refresh file size
    if (f->pos > inode.size)
    {
        read_inode(f->inode_id, &inode);    // get new inode(with new distributed inode_id)
        inode.size = f->pos;
        write_inode(f->inode_id, &inode);
    }

    spin_lock_release(&fs_lock);
    return written;
}

int do_close(int fd)
{
    // TODO [P6-task2]: Implement do_close
    spin_lock_acquire(&fs_lock);
    fdesc_array[fd].inode_id = 0;
    fdesc_array[fd].access = 0;
    fdesc_array[fd].pos = 0;
    spin_lock_release(&fs_lock);
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
{   // only change pos, not distribute data block
    // TODO [P6-task2]: Implement do_lseek
    spin_lock_acquire(&fs_lock);
    if (fd < 0 || fd >= NUM_FDESCS || fdesc_array[fd].inode_id == 0) 
    {
        printk("Error: Invalid fd %d\n", fd);
        spin_lock_release(&fs_lock);
        return -1;
    }
    fdesc_t *f = &fdesc_array[fd];
    inode_t inode;
    read_inode(f->inode_id, &inode);
    int new_pos = 0;
    
    // Calculate new position
    switch (whence) 
    {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = f->pos + offset;
            break;
        case SEEK_END:
            new_pos = inode.size + offset;
            break;
        default:
            printk("Error: Invalid whence %d\n", whence);
            spin_lock_release(&fs_lock);
            return -1;
    }
    // Note: It IS allowed to seek past the end of the file (creating holes)
    if (new_pos < 0) 
    {
        printk("Error: Seek position negative\n");
        spin_lock_release(&fs_lock);
        return -1;
    }

    f->pos = new_pos;
    spin_lock_release(&fs_lock);
    return new_pos;
}
