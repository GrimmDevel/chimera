/* =============================================================================
 * Chimera Operating System — Dynamic Host Configuration Protocol (DHCP)
 * kernel/net/dhcp.c
 * ============================================================================= */

#include <net/if.h>
#include <net/socket.h>
#include <net/protocols.h>
#include <kernel/panic.h>

#define DHCP_SERVER_PORT    67
#define DHCP_CLIENT_PORT    68
#define DHCP_MAGIC_COOKIE   0x63825363

#define DHCPDISCOVER        1
#define DHCPOFFER           2
#define DHCPREQUEST         3
#define DHCPACK             5

typedef struct CHIMERA_PACKED dhcp_packet {
    u8                  op;
    u8                  htype;
    u8                  hlen;
    u8                  hops;
    u32                 xid;
    u16                 secs;
    u16                 flags;
    struct in_addr      ciaddr;
    struct in_addr      yiaddr;
    struct in_addr      siaddr;
    struct in_addr      giaddr;
    u8                  chaddr[16];
    char                sname[64];
    char                file[128];
    u32                 magic;
    u8                  options[308];
} dhcp_packet_t;

extern void e1000_poll_rx(void);
extern chimera_error_t udp_sendto(socket_t *so, const void *buf, usize len, struct sockaddr_in *dest);

void dhcp_discover(ifnet_t *ifp) {
    if (!ifp || (ifp->if_flags & IFF_LOOPBACK)) return;

    socket_t *so = nullptr;
    if (socreate(AF_INET, &so, SOCK_DGRAM, IPPROTO_UDP) != CHIMERA_SUCCESS || !so) return;

    struct sockaddr_in client_sin = {0};
    client_sin.sin_family = AF_INET;
    client_sin.sin_port = htons(DHCP_CLIENT_PORT);
    client_sin.sin_addr.s_addr = 0;
    sobind(so, (struct sockaddr *)&client_sin);

    dhcp_packet_t pkt;
    __builtin_memset(&pkt, 0, sizeof(pkt));
    pkt.op = 1;
    pkt.htype = 1;
    pkt.hlen = 6;
    pkt.xid = htonl(0x3903F326);
    pkt.flags = htons(0x8000); // broadcast
    __builtin_memcpy(pkt.chaddr, ifp->if_mac, 6);
    pkt.magic = htonl(DHCP_MAGIC_COOKIE);

    // dhcp Options
    int opt_idx = 0;
    pkt.options[opt_idx++] = 53; // dhcp Message Type
    pkt.options[opt_idx++] = 1;
    pkt.options[opt_idx++] = DHCPDISCOVER;

    pkt.options[opt_idx++] = 55; // parameter Request List
    pkt.options[opt_idx++] = 3;
    pkt.options[opt_idx++] = 1;  // subnet Mask
    pkt.options[opt_idx++] = 3;  // router
    pkt.options[opt_idx++] = 6;  // dns

    pkt.options[opt_idx++] = 255; // end Option

    struct sockaddr_in bcast = {0};
    bcast.sin_family = AF_INET;
    bcast.sin_port = htons(DHCP_SERVER_PORT);
    bcast.sin_addr.s_addr = INADDR_BROADCAST;

    udp_sendto(so, &pkt, sizeof(pkt), &bcast);

    // poll for DHCPOFFER / DHCPACK
    for (int retry = 0; retry < 50; retry++) {
        e1000_poll_rx();
        if (so->so_rcv.sb_cc >= sizeof(dhcp_packet_t)) {
            dhcp_packet_t reply;
            usize out_len = 0;
            soreceive(so, nullptr, &reply, sizeof(reply), &out_len, 0);

            if (ntohl(reply.magic) == DHCP_MAGIC_COOKIE && reply.yiaddr.s_addr != 0) {
                ifp->if_ip = reply.yiaddr;

                // parse options
                u8 *opt = reply.options;
                while (*opt != 255 && (opt - reply.options) < 300) {
                    u8 code = *opt++;
                    if (code == 0) continue;
                    u8 len = *opt++;
                    if (code == 1 && len == 4) { // subnet Mask
                        __builtin_memcpy(&ifp->if_netmask.s_addr, opt, 4);
                    } else if (code == 3 && len >= 4) { // gateway
                        __builtin_memcpy(&ifp->if_gateway.s_addr, opt, 4);
                    } else if (code == 6 && len >= 4) { // dns
                        __builtin_memcpy(&ifp->if_dns.s_addr, opt, 4);
                    }
                    opt += len;
                }

                ifp->if_bcast.s_addr = ifp->if_ip.s_addr | ~ifp->if_netmask.s_addr;
                break;
            }
        }
        for (volatile int delay = 0; delay < 1000; delay++) cpu_relax();
    }

    soclose(so);

    // fallback default QEMU network if DHCP timed out
    if (ifp->if_ip.s_addr == 0) {
        ifp->if_ip.s_addr = htonl(0x0A00020F);       // 10.0.2.15
        ifp->if_netmask.s_addr = htonl(0xFFFFFF00);  // 255.255.255.0
        ifp->if_gateway.s_addr = htonl(0x0A000202);  // 10.0.2.2
        ifp->if_dns.s_addr = htonl(0x0A000203);      // 10.0.2.3
        ifp->if_bcast.s_addr = htonl(0x0A0002FF);    // 10.0.2.255
    }

    u8 *ip = (u8 *)&ifp->if_ip.s_addr;
    u8 *nm = (u8 *)&ifp->if_netmask.s_addr;
    u8 *gw = (u8 *)&ifp->if_gateway.s_addr;
    kprintf("        %s IP: %u.%u.%u.%u / %u.%u.%u.%u  (Gateway: %u.%u.%u.%u)\n",
            ifp->if_name, ip[0], ip[1], ip[2], ip[3], nm[0], nm[1], nm[2], nm[3], gw[0], gw[1], gw[2], gw[3]);
}
