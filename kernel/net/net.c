#include <e1000.h>
#include <type.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/list.h>
#include <os/smp.h>
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
        if (bytes_sent > 0) 
            break;       
        local_flush_dcache(); 
        uint32_t ims = e1000_read_reg(e1000, E1000_IMS);
        e1000_write_reg(e1000, E1000_IMS, ims | E1000_IMS_TXQE);
        local_flush_dcache();
        
        do_block(&(current_running[get_current_cpu_id()])->list, &send_block_queue);
    }
    return bytes_sent;  // Bytes it has transmitted
}

int do_net_recv(void *rxbuffer, int pkt_num, int *pkt_lens)
{
    // TODO: [p5-task2] Receive one network packet via e1000 device
    // TODO: [p5-task3] Call do_block when there is no packet on the way
    int recv_bytes = 0;
    int i=0;
    while(i < pkt_num)
    {
        int len = e1000_poll(rxbuffer);
        if(len <= 0)
        {
            printl("recv %d: no data, blocking...\n", i);
            local_flush_dcache();
            uint32_t ims = e1000_read_reg(e1000, E1000_IMS);
            e1000_write_reg(e1000, E1000_IMS, E1000_IMS_RXDMT0);
            local_flush_dcache();
            
            printl("begin block recv\n");
            do_block(&(current_running[get_current_cpu_id()])->list, &recv_block_queue);
            printl("get back recv\n");
        }
        else
        {
            pkt_lens[i] = len;
            recv_bytes += len;
            rxbuffer += len;
            i++;
            printl("recv %d success, len=%d\n", i-1, len);
        }
    }   
    return recv_bytes;  // Bytes it has received
}

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

    if (icr & E1000_ICR_RXDMT0)
    {
        printl("get rxdmt0 interrupt\n");
        e1000_handle_rxdmt0();
    }
}