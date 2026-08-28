// network protocol headers
#pragma once
#ifndef CHIMERA_NET_PROTOCOLS_H
#define CHIMERA_NET_PROTOCOLS_H

#include <kernel/chimera_types.h>
#include <net/if.h>

#define ETHERTYPE_IP    0x0800
#define ETHERTYPE_ARP   0x0806
#define ETHERTYPE_IPV6  0x86DD

typedef struct CHIMERA_PACKED ether_header {
    u8                  ether_dhost[ETHER_ADDR_LEN];
    u8                  ether_shost[ETHER_ADDR_LEN];
    u16                 ether_type;
} ether_header_t;

#define ARPHRD_ETHER    1
#define ARPOP_REQUEST   1
#define ARPOP_REPLY     2

typedef struct CHIMERA_PACKED ether_arp {
    u16                 ea_hdr;
    u16                 ea_pro;
    u8                  ea_hln;
    u8                  ea_pln;
    u16                 ea_op;
    u8                  arp_sha[6];
    u8                  arp_spa[4];
    u8                  arp_tha[6];
    u8                  arp_tpa[4];
} ether_arp_t;

typedef struct CHIMERA_PACKED ip_header {
    u8                  ip_vhl;
    u8                  ip_tos;
    u16                 ip_len;
    u16                 ip_id;
    u16                 ip_off;
    u8                  ip_ttl;
    u8                  ip_p;
    u16                 ip_sum;
    struct in_addr      ip_src;
    struct in_addr      ip_dst;
} ip_header_t;

#define IP_V(ip)        (((ip)->ip_vhl >> 4) & 0x0F)
#define IP_HL(ip)       ((ip)->ip_vhl & 0x0F)
#define IP_OFF_DF       0x4000
#define IP_OFF_MF       0x2000
#define IP_OFF_MASK     0x1FFF

#define ICMP_ECHOREPLY  0
#define ICMP_UNREACH    3
#define ICMP_ECHO       8
#define ICMP_TIMXCEED   11

typedef struct CHIMERA_PACKED icmp_header {
    u8                  icmp_type;
    u8                  icmp_code;
    u16                 icmp_cksum;
    union {
        struct {
            u16         id;
            u16         seq;
        } echo;
        u32             gateway;
        struct {
            u16         unused;
            u16         nextmtu;
        } frag;
    } icmp_hun;
} icmp_header_t;

typedef struct CHIMERA_PACKED udp_header {
    u16                 uh_sport;
    u16                 uh_dport;
    u16                 uh_ulen;
    u16                 uh_sum;
} udp_header_t;

#define TH_FIN          0x01
#define TH_SYN          0x02
#define TH_RST          0x04
#define TH_PUSH         0x08
#define TH_ACK          0x10
#define TH_URG          0x20

typedef struct CHIMERA_PACKED tcp_header {
    u16                 th_sport;
    u16                 th_dport;
    u32                 th_seq;
    u32                 th_ack;
    u8                  th_off_x2;
    u8                  th_flags;
    u16                 th_win;
    u16                 th_sum;
    u16                 th_urp;
} tcp_header_t;

#define TH_OFF(th)      (((th)->th_off_x2 >> 4) & 0x0F)

static inline u16 htons(u16 v) {
    return (u16)(((v & 0xFF) << 8) | ((v >> 8) & 0xFF));
}
static inline u16 ntohs(u16 v) { return htons(v); }

static inline u32 htonl(u32 v) {
    return ((v & 0xFF) << 24) | (((v >> 8) & 0xFF) << 16) |
           (((v >> 16) & 0xFF) << 8) | ((v >> 24) & 0xFF);
}
static inline u32 ntohl(u32 v) { return htonl(v); }

u16 in_checksum(const void *buf, usize len);
u16 in_pseudo_checksum(u32 src, u32 dst, u8 proto, u16 proto_len, const void *payload, usize payload_len);

#endif
