#ifndef ASIX_LWIPOPTS_H
#define ASIX_LWIPOPTS_H
#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_IGMP 0
#define LWIP_RAW 0
#define LWIP_UDP 1
#define LWIP_UDPLITE 0
#define LWIP_DHCP 1
#define LWIP_DHCP_MAX_DNS_SERVERS 1
/* Ask for broadcast offers/ACKs until configured. Some DHCP servers cannot
 * unicast to a client that has not yet acquired its offered address. Renewal
 * retains unicast semantics once the interface has an address. */
#define LWIP_HOOK_DHCP_APPEND_OPTIONS(netif, dhcp, state, msg, type, length) do { \
    if (((type) == DHCP_DISCOVER || (type) == DHCP_REQUEST) && \
        ip4_addr_isany_val(*netif_ip4_addr(netif))) \
        (msg)->flags = PP_HTONS(0x8000); \
} while (0)
#define LWIP_AUTOIP 0
#define LWIP_DNS 1
#define DNS_TABLE_SIZE 2
#define DNS_MAX_SERVERS 1
#define DNS_MAX_NAME_LENGTH 256
#define LWIP_TCP 1
#define IP_FORWARD 0
#define IP_REASSEMBLY 0
#define IP_FRAG 0
#define LWIP_STATS 0
#define LWIP_NETIF_HOSTNAME 0
#define LWIP_NETIF_STATUS_CALLBACK 0
#define LWIP_NETIF_LINK_CALLBACK 0
#define MEM_ALIGNMENT 4
#define MEM_SIZE (128 * 1024)
#define LWIP_RAM_HEAP_POINTER asix_lwip_heap
#define MEMP_MEM_MALLOC 1
#define MEMP_NUM_PBUF 32
#define PBUF_POOL_SIZE 16
#define PBUF_POOL_BUFSIZE 1536
#define MEMP_NUM_TCP_PCB 4
#define MEMP_NUM_TCP_PCB_LISTEN 1
#define MEMP_NUM_TCP_SEG 48
#define TCP_MSS 1460
#define TCP_SND_BUF (8 * TCP_MSS)
#define TCP_WND (8 * TCP_MSS)
#define TCP_SND_QUEUELEN 32
#define LWIP_WND_SCALE 0
#define TCP_QUEUE_OOSEQ 1
#define LWIP_TCP_TIMESTAMPS 0
#define LWIP_TCP_SACK_OUT 0
#define TCP_LISTEN_BACKLOG 0
#define LWIP_CHECKSUM_CTRL_PER_NETIF 0
#define CHECKSUM_GEN_IP 1
#define CHECKSUM_GEN_TCP 1
#define CHECKSUM_CHECK_IP 1
#define CHECKSUM_CHECK_TCP 1
#endif
