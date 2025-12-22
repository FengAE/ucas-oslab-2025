#include <e1000.h>
#include <type.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/list.h>
#include <os/smp.h>
#include <os/net.h>
#include <os/time.h>
#include <os/mm.h>
#include <csr.h>
#include <printk.h>

static LIST_HEAD(send_block_queue);
static LIST_HEAD(recv_block_queue);

int do_net_send(void *txpacket, int length)
{
    // TODO: [p5-task1] Transmit one network packet via e1000 device
    // TODO: [p5-task3] Call do_block when e1000 transmit queue is full
    // TODO: [p5-task4] Enable TXQE interrupt if transmit queue is full
    int bytes_sent;
    while (1) 
    {
        bytes_sent = e1000_transmit(txpacket, length);
        printl("bytes_sent: %d\n", bytes_sent);
        if (bytes_sent > 0) 
            break;       
        local_flush_dcache(); 
        uint32_t ims = e1000_read_reg(e1000, E1000_IMS);
        e1000_write_reg(e1000, E1000_IMS, ims | E1000_IMS_TXQE);
        local_flush_dcache();
        
        printl("bytes send 0, begin block\n");
        do_block(&(current_running[get_current_cpu_id()])->list, &send_block_queue);
    }
    return bytes_sent;  // Bytes it has transmitted
}

static inline void local_irq_save(uint64_t *flags) {
    uint64_t sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    *flags = sstatus;
    asm volatile("csrw sstatus, %0" :: "r"(sstatus & ~SR_SIE));
}

static inline void local_irq_restore(uint64_t flags) {
    asm volatile("csrw sstatus, %0" :: "r"(flags));
}

int do_net_recv(void *rxbuffer, int pkt_num, int *pkt_lens)
{
    int recv_bytes = 0;
    int i = 0;
    uint64_t flags; // Store interrupt status

    while(i < pkt_num)
    {
        int len = e1000_poll(rxbuffer);
        if (len > 0) goto save_packet;

        // not recv: ready to block
        local_flush_dcache();
        e1000_write_reg(e1000, E1000_IMS, E1000_IMS_RXDMT0);
        local_flush_dcache();

        // Atomic operation: close SIE
        local_irq_save(&flags);

        // 4. Double Check
        len = e1000_poll(rxbuffer);
        if (len > 0) {
            local_irq_restore(flags); // Restore SIE
            goto save_packet;
        }
        printl("recv %d: blocking...\n", i);
        do_block(&(current_running[get_current_cpu_id()])->list, &recv_block_queue);
        local_irq_restore(flags);
        continue;

save_packet:
        pkt_lens[i] = len;
        recv_bytes += len;
        rxbuffer += len;
        i++;
        printl("recv %d success, len=%d\n", i-1, len);
    }   
    return recv_bytes;
}

// ======================= Recv Stream =======================
char *stream_buffer = NULL;
uint8_t *stream_received = NULL;
char *poll_buffer;
int current_seq = 0;
int seen_seq = 0;
uint64_t last_rsd_time = 0;
uint8_t header_cache[54]; 
int has_valid_header = 0;

void net_init_buffers() 
{
    if (stream_buffer == NULL) 
    {
        stream_buffer = (char *)allocPage(STREAM_BUF_SIZE / PAGE_SIZE); 
        stream_received = (uint8_t *)allocPage(STREAM_BUF_SIZE / PAGE_SIZE);
        poll_buffer = (char*)allocPage(POLL_BUF_SIZE / PAGE_SIZE);
        memset(stream_received, 0, STREAM_BUF_SIZE);
    }
}

uint16_t ip_checksum(void *vdata, size_t length) {
    char *data = (char *)vdata;
    uint32_t acc = 0xffff;
    for (size_t i = 0; i + 1 < length; i += 2) {
        uint16_t word;
        memcpy((uint8_t*)&word, data + i, 2);
        acc += ntohs(word);
        if (acc > 0xffff) {
            acc -= 0xffff;
        }
    }
    if (length & 1) {
        uint16_t word = 0;
        memcpy((uint8_t*)&word, data + length - 1, 1);
        acc += ntohs(word);
        if (acc > 0xffff) {
            acc -= 0xffff;
        }
    }
    return htons(~acc);
}

void send_control_packet(uint8_t flag, uint32_t seq) 
{
    if (!has_valid_header) return;

    char packet[100]; 
    memcpy(packet, header_cache, 54);
    memcpy(packet, header_cache + 6, 6);       // Dst(0-5) = Old Src
    memcpy(packet + 6, header_cache, 6);       // Src(6-11) = Old Dst
    memcpy(packet + 26, header_cache + 30, 4); // IP Src = Old IP Dst
    memcpy(packet + 30, header_cache + 26, 4); // IP Dst = Old IP Src
    memcpy(packet + 34, header_cache + 36, 2); // Port Src = Old Port Dst
    memcpy(packet + 36, header_cache + 34, 2); // Port Dst = Old Port Src
    uint16_t ip_total_len = 20 + 20 + sizeof(stream_header_t);
    *(uint16_t *)(packet + 16) = htons(ip_total_len);
    *(uint16_t *)(packet + 24) = 0; 
    uint16_t checksum = ip_checksum(packet + 14, 20);
    *(uint16_t *)(packet + 24) = checksum;
    *(uint16_t *)(packet + 50) = 0; 

    stream_header_t *header = (stream_header_t *)(packet + 54);
    header->magic = MAGIC_NUM;
    header->flags = flag;
    header->length = htons(0); 
    header->seq = htonl(seq);

    do_net_send(packet, 54 + sizeof(stream_header_t));
}

int do_net_recv_stream(void* buffer, int nbytes)
{
    if (last_rsd_time == 0) last_rsd_time = get_timer();
    while(1)
    {
        while(1)
        {
            int len = e1000_poll(poll_buffer);
            printl("poll length: %d\n", len);
            if(len<=0)  break;
            stream_header_t *hdr = (stream_header_t*)(poll_buffer+54);  // 54 (Ethernet 14 + IP 20 + TCP 20)
            
            // printl("HEX: ");
            // for (int i = 0; i < 16; i++) {
            //         printl("%02x ", ((uint8_t *)hdr)[i]);
            // }
            // printl("\n");

            // printl("magic: %x, flags: %x\n", hdr->magic, hdr->flags);
            if((hdr->magic!=MAGIC_NUM) ||(hdr->flags!=FLAG_DAT))    continue;
            memcpy(header_cache, poll_buffer, 54);
            has_valid_header = 1;

            uint16_t data_len = ntohs(hdr->length);
            uint32_t seq = ntohl(hdr->seq);
            printl("seq: %d, data_len: %d\n", seq, data_len);
            if (seq + data_len >= STREAM_BUF_SIZE)
            {
                printl("Warning: Stream buffer overflow!\n");
                continue; 
            }

            // Store kernel buffer
            if(seq >= current_seq)
            {
                printl("begin fill kernel buffer, len: %d\n", data_len);
                memcpy(&stream_buffer[seq], (uint8_t*)hdr+sizeof(stream_header_t), data_len);
                memset(&stream_received[seq], 1, data_len);
                if(seq + data_len > seen_seq)   seen_seq = seq+data_len;
            }
            else    // maybe not recieve previous ack?
                send_control_packet(FLAG_ACK, current_seq);
        }
        // check if have continous data to provide to user
        int copy_len = 0;
        while(current_seq + copy_len < STREAM_BUF_SIZE 
            && stream_received[current_seq + copy_len] == 1) 
        {
            copy_len++;
            if (copy_len >= nbytes) break;
        }

        if(copy_len > 0)
        {
            printl("Continous data, begin: %d, end: %d\n", current_seq, current_seq+copy_len);
            memcpy(buffer, &stream_buffer[current_seq], copy_len);
            current_seq += copy_len;
            send_control_packet(FLAG_ACK, current_seq);
            printl("Copy end, return to user\n");
            return copy_len;
        }
        // no continuous data: deal RSD, and block
        uint64_t now = get_timer();

        if (now - last_rsd_time > 0) 
        {
            printl("Timeout! Ask for %d\n", current_seq);
            send_control_packet(FLAG_RSD, current_seq);
            last_rsd_time = now;
        }

        local_flush_dcache();
        uint32_t ims = e1000_read_reg(e1000, E1000_IMS);
        e1000_write_reg(e1000, E1000_IMS, ims | E1000_IMS_RXDMT0 | E1000_IMS_RXT0);
        local_flush_dcache();

        printl("Stream blocking: wait seq %d\n", current_seq);
        do_block(&(current_running[get_current_cpu_id()])->list, &recv_block_queue);
    }
}

// ================== Net Interrupts =====================
void e1000_handle_txqe()
{
    while(send_block_queue.next != &send_block_queue)
        do_unblock(&send_block_queue);
}

void e1000_handle_rxdmt0()
{
    while(recv_block_queue.next != &recv_block_queue)
        do_unblock(&recv_block_queue);
}

void net_handle_irq(void)
{
    // TODO: [p5-task4] Handle interrupts from network device
    local_flush_dcache();
    uint32_t icr = e1000_read_reg(e1000, E1000_ICR);
    if (icr & E1000_ICR_TXQE) 
    {
        e1000_handle_txqe();
        e1000_write_reg(e1000, E1000_IMC, E1000_IMC_TXQE);
        local_flush_dcache();
    }

    if (icr & (E1000_ICR_RXDMT0|E1000_ICR_RXT0))
    {
        printl("get rxdmt0 interrupt\n");
        e1000_handle_rxdmt0();
    }
}