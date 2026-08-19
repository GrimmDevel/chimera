/* =============================================================================
 * XIU Operating System — Internet Protocol Version 4 (IPv4)
 * kernel/net/ip.c
 * ============================================================================= */

#include <net/if.h>
#include <net/mbuf.h>
#include <net/protocols.h>
#include <kernel/panic.h>

static u16 s_ip_id_counter = 1;

u16 in_checksum(const void *buf, usize len) {
    const u16 *ptr = (const u16 *)buf;
    u32 sum = 0;

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const u8 *)ptr;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (u16)(~sum);
}

u16 in_pseudo_checksum(u32 src, u32 dst, u8 proto, u16 proto_len, const void *payload, usize payload_len) {
    u32 sum = 0;
    sum += (src >> 16) & 0xFFFF;
    sum += src & 0xFFFF;
    sum += (dst >> 16) & 0xFFFF;
    sum += dst & 0xFFFF;
    sum += htons((u16)proto);
    sum += htons(proto_len);

    const u16 *ptr = (const u16 *)payload;
    usize len = payload_len;
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const u8 *)ptr;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (u16)(~sum);
}

extern void icmp_input(ifnet_t *ifp, mbuf_t *m, ip_header_t *ip);
extern void udp_input(ifnet_t *ifp, mbuf_t *m, ip_header_t *ip);
extern void tcp_input(ifnet_t *ifp, mbuf_t *m, ip_header_t *ip);

void ip_input(ifnet_t *ifp, mbuf_t *m) {
    if (!ifp || !m || m->m_len < (i32)sizeof(ip_header_t)) {
        m_freem(m);
        return;
    }

    ip_header_t ip;
    __builtin_memcpy(&ip, m->m_data, sizeof(ip_header_t));

    if (IP_V(&ip) != 4) {
        m_freem(m);
        return;
    }

    u16 hlen = (u16)(IP_HL(&ip) * 4);
    if (hlen < sizeof(ip_header_t) || m->m_len < (i32)hlen) {
        m_freem(m);
        return;
    }

    // advance buffer past IP header
    m->m_data += hlen;
    m->m_len -= hlen;
    m->m_pkthdr.len -= hlen;

    switch (ip.ip_p) {
        case IPPROTO_ICMP:
            icmp_input(ifp, m, &ip);
            break;
        case IPPROTO_UDP:
            udp_input(ifp, m, &ip);
            break;
        case IPPROTO_TCP:
            tcp_input(ifp, m, &ip);
            break;
        default:
            m_freem(m);
            break;
    }
}

xiu_error_t ip_output(mbuf_t *m, struct in_addr src_ip, struct in_addr dst_ip, u8 proto) {
    if (!m) return XIU_ERR_INVALID;

    ifnet_t *ifp = nullptr;
    if (dst_ip.s_addr == htonl(INADDR_LOOPBACK)) {
        ifp = if_lookup("lo0");
    } else {
        ifp = if_get_default();
    }

    if (!ifp) {
        m_freem(m);
        return XIU_ERR_NOT_FOUND;
    }

    if (src_ip.s_addr == 0) {
        src_ip = ifp->if_ip;
    }

    // prepend IP Header
    mbuf_t *hdr = m_gethdr(MT_HEADER);
    if (!hdr) {
        m_freem(m);
        return XIU_ERR_NOMEM;
    }

    ip_header_t *ip = (ip_header_t *)hdr->m_data;
    ip->ip_vhl = (4 << 4) | (sizeof(ip_header_t) / 4);
    ip->ip_tos = 0;
    ip->ip_len = htons((u16)(sizeof(ip_header_t) + m->m_pkthdr.len));
    ip->ip_id = htons(s_ip_id_counter++);
    ip->ip_off = htons(IP_OFF_DF);
    ip->ip_ttl = 64;
    ip->ip_p = proto;
    ip->ip_sum = 0;
    ip->ip_src = src_ip;
    ip->ip_dst = dst_ip;
    ip->ip_sum = in_checksum(ip, sizeof(ip_header_t));

    hdr->m_len = sizeof(ip_header_t);
    hdr->m_pkthdr.len = m->m_pkthdr.len + sizeof(ip_header_t);
    hdr->m_next = m;

    if (ifp->if_flags & IFF_LOOPBACK) {
        return ifp->if_output(ifp, hdr, dst_ip);
    }

    extern xiu_error_t ethernet_output(ifnet_t *ifp, mbuf_t *m, struct in_addr dest_ip, u16 ethertype);
    return ethernet_output(ifp, hdr, dst_ip, ETHERTYPE_IP);
}
