#include <stdio.h>
#include <string.h>
#include <unistd.h>

// 130 * 1024 * 1024 = 136314880
#define LARGE_OFFSET  136314880 

char buffer[128];

void test_basic_lseek()
{
    printf("=== Test 1: Basic lseek ===\n");
    int fd = sys_open("basic.txt", O_RDWR);
    if (fd < 0) { printf("Open failed\n"); return; }

    sys_write(fd, "Hello", 5);
    // 此时 pos=5。测试 SEEK_CUR，向后跳过 1 个字节（形成一个小空洞）
    // 预期 pos 变为 6
    sys_lseek(fd, 1, SEEK_CUR);

    sys_write(fd, "World", 5);

    // 4. 测试 SEEK_SET，回到开头
    sys_lseek(fd, 0, SEEK_SET);

    // 5. 读取全部 (应为 5+1+5 = 11 bytes)
    memset(buffer, 0, sizeof(buffer));
    int len = sys_read(fd, buffer, 11);
    
    printf("Read %d bytes: ", len);
    for(int i=0; i<len; i++) 
    {
        // 空洞应该读出 0
        if (buffer[i] == 0) printf("_"); // 用 _ 表示 0
        else printf("%c", buffer[i]);
    }
    printf("\n");
    // 预期输出: Hello_World

    sys_close(fd);
}

void test_large_file()
{
    printf("\n=== Test 2: Large File (>128MB) & Indirect Index ===\n");
    int fd = sys_open("huge.txt", O_RDWR);
    if (fd < 0) { printf("Open failed\n"); return; }

    // 1. 在文件头部写入数据 (位于 Direct Block 或 L1)
    char *head_str = "This is the HEAD.";
    sys_write(fd, head_str, strlen(head_str));
    printf("Written HEAD at offset 0.\n");

    // 2. 使用 lseek 跨越 128MB 边界
    // 这将迫使文件系统分配二级间接块 (Double Indirect Block)
    printf("Seeking to offset %d (130MB)...\n", LARGE_OFFSET);
    int new_pos = sys_lseek(fd, LARGE_OFFSET, SEEK_SET);
    
    if (new_pos != LARGE_OFFSET) {
        printf("Error: lseek returned %d, expected %d\n", new_pos, LARGE_OFFSET);
    }

    // 3. 在 130MB 处写入数据 (位于 L2 Block)
    char *tail_str = "This is the TAIL.";
    sys_write(fd, tail_str, strlen(tail_str));
    printf("Written TAIL at offset 130MB.\n");

    // --- 验证阶段 ---

    // 4. 回到头部读取验证
    sys_lseek(fd, 0, SEEK_SET);
    memset(buffer, 0, sizeof(buffer));
    sys_read(fd, buffer, strlen(head_str));
    printf("Read back HEAD: %s\n", buffer);
    if (strcmp(buffer, head_str) != 0) printf("FAIL: Head content mismatch!\n");

    // 5. 跳到尾部读取验证
    sys_lseek(fd, LARGE_OFFSET, SEEK_SET);
    memset(buffer, 0, sizeof(buffer));
    sys_read(fd, buffer, strlen(tail_str));
    printf("Read back TAIL: %s\n", buffer);
    if (strcmp(buffer, tail_str) != 0) printf("FAIL: Tail content mismatch!\n");

    // 6. (可选) 验证中间的空洞
    // 读中间随便一个位置，比如 1MB 处，应该是全 0
    sys_lseek(fd, 1024*1024, SEEK_SET);
    char byte_check;
    sys_read(fd, &byte_check, 1);
    if (byte_check == 0) printf("Hole check (1MB): OK (Value is 0)\n");
    else printf("Hole check (1MB): FAIL (Value is %d)\n", byte_check);

    sys_close(fd);
}

void test_seek_end()
{
    printf("\n=== Test 3: SEEK_END ===\n");
    int fd = sys_open("huge.txt", O_RDWR); // 打开刚才的大文件
    if (fd < 0) return;

    // 此时文件大小应该是 LARGE_OFFSET + strlen("This is the TAIL.")
    // 大约是 136314880 + 17 = 136314897

    // 1. 跳到文件末尾再往后偏移 10 字节
    int expected_pos = LARGE_OFFSET + 17 + 10;
    int pos = sys_lseek(fd, 10, SEEK_END);
    
    printf("Seek END + 10, current pos: %d (Expected: %d)\n", pos, expected_pos);

    // 2. 写入结束标记
    sys_write(fd, "END", 3);
    
    sys_close(fd);
}

int main()
{
    test_basic_lseek();
    test_large_file();
    test_seek_end();
    return 0;
}