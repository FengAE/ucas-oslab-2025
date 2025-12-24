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
int current_seq = 0;             
int highest_seq = 0;             
uint64_t last_progress_time = 0; 
uint64_t last_rsd_time = 0;      
uint64_t rsd_timeout = 0;        

uint8_t header_cache[54]; 
int has_valid_header = 0;
char *poll_buffer = NULL;

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

void stream_try_advance() 
{   // check if current_seq if full, if is: move forward
    int progressed = 0;
    while (current_seq < STREAM_BUF_SIZE && stream_received[current_seq]) {
        int step = 1;
        while(current_seq + step < STREAM_BUF_SIZE && stream_received[current_seq + step]) {
            step++;
        }
        current_seq += step;
        progressed = 1;
    }

    if (progressed) 
    {
        last_progress_time = get_ticks();
        send_control_packet(FLAG_ACK, current_seq);
    }
}

void stream_handle_packet(char *pkt_buffer) 
{
    stream_header_t *hdr = (stream_header_t*)(pkt_buffer + 54);
    if(hdr->magic != MAGIC_NUM || hdr->flags != FLAG_DAT) return;
    // Store header
    if (!has_valid_header) 
    {
        memcpy(header_cache, pkt_buffer, 54);
        has_valid_header = 1;
        last_progress_time = get_ticks(); // first receive pkg
    }
    uint16_t data_len = ntohs(hdr->length);
    uint32_t seq = ntohl(hdr->seq);
    if (seq + data_len > highest_seq) highest_seq = seq + data_len;

    // 1. receive old pkg
    if (seq < current_seq) 
    {
        send_control_packet(FLAG_ACK, current_seq);
        return;
    }

    // 2. receive future or cur pkg
    if (seq + data_len < STREAM_BUF_SIZE) 
    {
        if (stream_received[seq] == 0) 
        {
            memcpy(&stream_buffer[seq], (uint8_t*)hdr + sizeof(stream_header_t), data_len);
            memset(&stream_received[seq], 1, data_len);
        }
        if (seq == current_seq) {
            stream_try_advance();
        }
    }
}

void stream_check_timeout() 
{   // check if send RSD
    if (!has_valid_header) return;
    if (current_seq >= highest_seq) return;

    uint64_t now = get_ticks();
    if (now - last_progress_time > rsd_timeout && 
        now - last_rsd_time > rsd_timeout) 
    {
        printl("Timeout! Gap detected at %d, Highest %d. Sending RSD.\n", current_seq, highest_seq);
        send_control_packet(FLAG_RSD, current_seq);
        last_rsd_time = now;
    }
}

int do_net_recv_stream(void* buffer, int nbytes)
{
    uint64_t flags; 
    while(1)
    {
        while (1) 
        {
            int len = e1000_poll(poll_buffer);
            if (len <= 0) break;
            stream_handle_packet(poll_buffer);
        }
        
        static int user_copied_seq = 0;
        
        int available = current_seq - user_copied_seq;
        if (available > 0) 
        {
            int copy_len = (available > nbytes) ? nbytes : available;
            memcpy(buffer, &stream_buffer[user_copied_seq], copy_len);
            user_copied_seq += copy_len;
            return copy_len;
        }

        stream_check_timeout();
        local_flush_dcache();
        e1000_write_reg(e1000, E1000_IMS, E1000_IMS_RXDMT0);
        local_flush_dcache();

        local_irq_save(&flags);
        
        // Double Check
        int len = e1000_poll(poll_buffer);
        if (len > 0) 
        {
            local_irq_restore(flags);
            stream_handle_packet(poll_buffer);
            continue;
        }
        printl("Stream blocking: wait seq %d\n", current_seq);
        do_block(&(current_running[get_current_cpu_id()])->list, &recv_block_queue);
        local_irq_restore(flags);
        // do_sleep(2);    // use time check, since PYNQ not support RXT0
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
    printl("icr: %x\n", icr);
    if (icr & E1000_ICR_TXQE) 
    {
        e1000_handle_txqe();
        e1000_write_reg(e1000, E1000_IMC, E1000_IMC_TXQE);
        local_flush_dcache();
    }

    printl("icr & (E1000_ICR_RXDMT0): %d\n", icr & (E1000_ICR_RXDMT0));
    printl("icr & (E1000_ICR_RXT0): %d\n", icr & (E1000_ICR_RXT0));
    if (icr & (E1000_ICR_RXDMT0|E1000_ICR_RXT0))
    {
        printl("get rxdmt0 interrupt\n");
        e1000_handle_rxdmt0();
    }
}