#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#define RCV_BUF_SIZE (64 * 1024) 

// uint16_t fletcher16(const uint8_t *data, int len) 
// {
//     uint16_t sum1 = 0;
//     uint16_t sum2 = 0;
//     for (int i = 0; i < len; ++i) {
//         sum1 = (sum1 + data[i]) % 255;
//         sum2 = (sum2 + sum1) % 255;
//     }
//     return (sum2 << 8) | sum1;
// }

uint8_t recv_buffer[RCV_BUF_SIZE];

int main(void)
{
    int file_len = 0;
    int total_received = 0;
    int nbytes = 0;
    
    printf("Starting reliable file receiver...\n");

    // read head 4 bytes: file size
    int size_header_received = 0;
    uint8_t size_buf[4];
    
    while(size_header_received < 4) 
    {
        int want = 4 - size_header_received;
        int recv = sys_net_recv_stream(size_buf + size_header_received, want);
        
        if(recv > 0) 
            size_header_received += recv;
    }
    file_len = (size_buf[0] << 24) | (size_buf[1] << 16) | (size_buf[2] << 8) | size_buf[3];
    printf("File size: %d bytes\n", file_len);
    
    uint16_t sum1 = 0, sum2 = 0; 

    while (total_received < file_len) 
    {
        int chunk_size = RCV_BUF_SIZE;
        if (file_len - total_received < chunk_size)
            chunk_size = file_len - total_received;
        int recv = sys_net_recv_stream(recv_buffer, chunk_size);
        printf("Received: %d bytes\n", recv);
        if (recv > 0) 
        {
            for (int i = 0; i < recv; i++) 
            {
                sum1 = (sum1 + recv_buffer[i]) % 255;
                sum2 = (sum2 + sum1) % 255;
            }        
            total_received += recv;
        }
    }

    uint16_t checksum = (sum2 << 8) | sum1;
    printf("\nTransfer complete!\n");
    printf("Total Received: %d bytes\n", total_received);
    printf("Fletcher Checksum: 0x%04x\n", checksum);   
    
    return 0;
}