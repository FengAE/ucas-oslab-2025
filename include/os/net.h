#ifndef __INCLUDE_NET_H__
#define __INCLUDE_NET_H__

#include <os/list.h>
#include <type.h>

#define PKT_NUM 32

#define ETH_ALEN 6u                 // Length of MAC address
#define ETH_P_IP 0x0800u            // IP protocol
// Ethernet header
struct ethhdr {
    uint8_t ether_dmac[ETH_ALEN];   // destination mac address
    uint8_t ether_smac[ETH_ALEN];   // source mac address
    uint16_t ether_type;            // protocol format
};

void net_handle_irq(void);
int do_net_recv(void *rxbuffer, int pkt_num, int *pkt_lens);
int do_net_send(void *txpacket, int length);
int do_net_recv_stream(void* buffer, int nbytes);
void e1000_handle_txqe();
void e1000_handle_rxdmt0();

// [p5-task4] recv_stream
extern void net_init_buffers();
#pragma pack(push, 1)
typedef struct {
    uint8_t magic;      // 0x45
    uint8_t flags;      // 0:DAT, 1:ACK, 2:RSD
    uint16_t length;   
    uint32_t seq;       
}stream_header_t;
#pragma pack(pop)

#define MAGIC_NUM 0x45
#define FLAG_ACK 0
#define FLAG_DAT 1
#define FLAG_RSD 2

// edian transform
#define ntohs(x) ((((x) & 0xFF) << 8) | (((x) >> 8) & 0xFF))
#define htons(x) ntohs(x)
#define ntohl(x) ((((x) & 0xFF) << 24) | (((x) & 0xFF00) << 8) | \
                  (((x) >> 8) & 0xFF00) | (((x) >> 24) & 0xFF))
#define htonl(x) ntohl(x)

#define STREAM_BUF_SIZE (64 * 4096)
#define POLL_BUF_SIZE 4096

#endif  // __INCLUDE_NET_H__
