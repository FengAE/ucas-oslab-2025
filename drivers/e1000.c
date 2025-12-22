#include <e1000.h>
#include <type.h>
#include <os/string.h>
#include <os/time.h>
#include <os/net.h>
#include <assert.h>
#include <pgtable.h>

// E1000 Registers Base Pointer
volatile uint8_t *e1000;  // use virtual memory address

// E1000 Tx & Rx Descriptors
static struct e1000_tx_desc tx_desc_array[TXDESCS] __attribute__((aligned(16)));
static struct e1000_rx_desc rx_desc_array[RXDESCS] __attribute__((aligned(16)));

// E1000 Tx & Rx packet buffer
static char tx_pkt_buffer[TXDESCS][TX_PKT_SIZE];
static char rx_pkt_buffer[RXDESCS][RX_PKT_SIZE];

// Fixed Ethernet MAC Address of E1000
static const uint8_t enetaddr[6] = {0x00, 0x0a, 0x35, 0x00, 0x1e, 0x53};

/**
 * e1000_reset - Reset Tx and Rx Units; mask and clear all interrupts.
 **/
static void e1000_reset(void)
{
    local_flush_dcache();
	/* Turn off the ethernet interface */
    e1000_write_reg(e1000, E1000_RCTL, 0);
    e1000_write_reg(e1000, E1000_TCTL, 0);

	/* Clear the transmit ring */
    e1000_write_reg(e1000, E1000_TDH, 0);
    e1000_write_reg(e1000, E1000_TDT, 0);

	/* Clear the receive ring */
    e1000_write_reg(e1000, E1000_RDH, 0);
    e1000_write_reg(e1000, E1000_RDT, 0);

	/**
     * Delay to allow any outstanding PCI transactions to complete before
	 * resetting the device
	 */
    latency(1);

	/* Clear interrupt mask to stop board from generating interrupts */
    e1000_write_reg(e1000, E1000_IMC, 0xffffffff);

    /* Clear any pending interrupt events. */
    while (0 != e1000_read_reg(e1000, E1000_ICR)) ;
    local_flush_dcache();
}

/**
 * e1000_configure_tx - Configure 8254x Transmit Unit after Reset
 **/
static void e1000_configure_tx(void)
{
    /* TODO: [p5-task1] Initialize tx descriptors */
    for (int i = 0; i < TXDESCS; i++) 
    {
        memset(&tx_desc_array[i], 0, sizeof(struct e1000_tx_desc));
        tx_desc_array[i].addr = kva2pa((uintptr_t)tx_pkt_buffer[i]); 
        tx_desc_array[i].status  = E1000_TXD_STAT_DD;   // free
    }
    /* TODO: [p5-task1] Set up the Tx descriptor base address and length */
    uintptr_t tx_base = kva2pa((uintptr_t)tx_desc_array);
    // printl("TX Base PA: %lx\n", tx_base);
    e1000_write_reg(e1000, E1000_TDBAL, (uint32_t)(tx_base & 0x00000000ffffffff));
    e1000_write_reg(e1000, E1000_TDBAH, (uint32_t)((tx_base & 0xffffffff00000000)>>32));
    e1000_write_reg(e1000, E1000_TDLEN, TXDESCS * sizeof(struct e1000_tx_desc));

    e1000_write_reg(e1000, E1000_TIPG, 0x0060200A);

	/* TODO: [p5-task1] Set up the HW Tx Head and Tail descriptor pointers */
    e1000_write_reg(e1000, E1000_TDH, 0);
    e1000_write_reg(e1000, E1000_TDT, 0);

    /* TODO: [p5-task1] Program the Transmit Control Register */
    e1000_write_reg(e1000, E1000_TCTL,
        (0x40 << 12) | (0x10 << 4) | E1000_TCTL_PSP | E1000_TCTL_EN);
    local_flush_dcache();
}

/**
 * e1000_configure_rx - Configure 8254x Receive Unit after Reset
 **/
static void e1000_configure_rx(void)
{
    /* TODO: [p5-task2] Set e1000 MAC Address to RAR[0] */
    e1000_write_reg_array(e1000, E1000_RA, 0, 
                        (enetaddr[3]<<24) | (enetaddr[2]<<16) | (enetaddr[1]<<8) | enetaddr[0]);
    e1000_write_reg_array(e1000, E1000_RA, 1,
                          E1000_RAH_AV | (enetaddr[5]<<8) | enetaddr[4]);
    /* TODO: [p5-task2] Initialize rx descriptors */
    for (int i = 0; i < RXDESCS; i++) 
    {
        memset(&rx_desc_array[i], 0, sizeof(struct e1000_rx_desc));
        rx_desc_array[i].addr = kva2pa((uintptr_t)rx_pkt_buffer[i]);
    }
    /* TODO: [p5-task2] Set up the Rx descriptor base address and length */
    uintptr_t rx_base = kva2pa((uintptr_t)rx_desc_array);
    e1000_write_reg(e1000, E1000_RDBAL, (uint32_t)(rx_base & 0xFFFFFFFF));
    e1000_write_reg(e1000, E1000_RDBAH, (uint32_t)(rx_base >> 32));
    e1000_write_reg(e1000, E1000_RDLEN, RXDESCS * sizeof(struct e1000_rx_desc));
    /* TODO: [p5-task2] Set up the HW Rx Head and Tail descriptor pointers */
    e1000_write_reg(e1000, E1000_RDH, 0);
    e1000_write_reg(e1000, E1000_RDT, RXDESCS - 1);
    /* TODO: [p5-task2] Program the Receive Control Register */
    e1000_write_reg(e1000, E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SZ_2048);
    /* TODO: [p5-task4] Enable RXDMT0 Interrupt */
    e1000_write_reg(e1000, E1000_IMS, E1000_IMS_RXDMT0 | E1000_IMS_RXT0);
    local_flush_dcache();
}

/**
 * e1000_init - Initialize e1000 device and descriptors
 **/
void e1000_init(void)
{
    /* Reset E1000 Tx & Rx Units; mask & clear all interrupts */
    e1000_reset();

    /* Configure E1000 Tx Unit */
    e1000_configure_tx();

    /* Configure E1000 Rx Unit */
    e1000_configure_rx();

    net_init_buffers();
}

/**
 * e1000_transmit - Transmit packet through e1000 net device
 * @param txpacket - The buffer address of packet to be transmitted
 * @param length - Length of this packet
 * @return - Number of bytes that are transmitted successfully
 **/
int e1000_transmit(void *txpacket, int length)
{
    /* TODO: [p5-task1] Transmit one packet from txpacket */
    local_flush_dcache();
    uint32_t tail = e1000_read_reg(e1000, E1000_TDT);

    uint32_t tctl = e1000_read_reg(e1000, E1000_TCTL);

    // fill describer
    struct e1000_tx_desc *desc = &tx_desc_array[tail];
    if (!(desc->status & E1000_TXD_STAT_DD))
        return -1;   // ring full
    if (length <= 0)
        return -1;

    desc->length = length > TX_PKT_SIZE ? TX_PKT_SIZE : length;
    memcpy((uint8_t*)tx_pkt_buffer[tail], txpacket, desc->length);
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS; 
    desc->status &= ~E1000_TXD_STAT_DD;   // dd=0: not finished  

    e1000_write_reg(e1000, E1000_TDT, (tail+1)%TXDESCS);
    local_flush_dcache();

    uint32_t tdh = e1000_read_reg(e1000, E1000_TDH);
    uint32_t tdt = e1000_read_reg(e1000, E1000_TDT);
    // printl("TX Debug: Length=%d, TDT=%d, TDH=%d\n", length, tdt, tdh);
    local_flush_dcache();
    return desc->length;
}

/**
 * e1000_poll - Receive packet through e1000 net device
 * @param rxbuffer - The address of buffer to store received packet
 * @return - Length of received packet
 **/
static int rx_cur = 0;
int e1000_poll(void *rxbuffer)
{
    /* TODO: [p5-task2] Receive one packet and put it into rxbuffer */
    local_flush_dcache();
    struct e1000_rx_desc *desc = &rx_desc_array[rx_cur];

    uint32_t rctl = e1000_read_reg(e1000, E1000_RCTL);
    printl("1\n");
    if ((desc->status & E1000_RXD_STAT_DD) == 0)    // not recv pkg
        return 0;

    int len = desc->length;
    uint8_t *src = (uint8_t *)rx_pkt_buffer[rx_cur];
    uint8_t *dst = (uint8_t *)rxbuffer;
    for (int k = 0; k < len; k++) {
        dst[k] = src[k];
    }
    desc->status &= ~E1000_RXD_STAT_DD;
    desc->length = 0;
    
    int old_cur = rx_cur;
    rx_cur = (rx_cur + 1) % RXDESCS;
    e1000_write_reg(e1000, E1000_RDT, old_cur);   
    printl("3\n"); 
    local_flush_dcache();
    uint32_t rdh = e1000_read_reg(e1000, E1000_RDH);
    uint32_t rdt = e1000_read_reg(e1000, E1000_RDT);
    // printl("RX: Length=%d, RDT=%d, RDH=%d\n", len, rdt, rdh);
    local_flush_dcache();
    return len;
}