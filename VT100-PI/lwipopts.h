// Minimal lwIP config for a bare-metal (NO_SYS) TCP client used in poll mode,
// alongside the CYW43 wireless driver. Based on the pico-examples picow client.
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
// Memory is tight (the smooth-scroll framebuffer is large), and a telnet
// terminal is low-bandwidth, so keep the pools modest. PBUF_POOL_SIZE dominates
// RAM (~1.5 KB each); 12 buffers is ample for a single interactive session.
#define MEM_SIZE                    5000
#define MEMP_NUM_TCP_SEG            16
#define MEMP_NUM_ARP_QUEUE          6
#define PBUF_POOL_SIZE              12
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    0
#define TCP_WND                     (4 * TCP_MSS)
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (2 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETCONN                0
#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0
#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1
#define LWIP_TCP_KEEPALIVE          1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

#endif // LWIPOPTS_H
