/* =============================================================================
 * XIU Operating System — Ethernet Frame Processing
 * kernel/net/ethernet.c
 * ============================================================================= */

#include <net/if.h>
#include <net/mbuf.h>
#include <net/protocols.h>

extern void arp_input(ifnet_t *ifp, mbuf_t *m);
extern void ip_input(ifnet_t *ifp, mbuf_t *m);
extern xiu_error_t arp_resolve(ifnet_t *ifp, struct in_addr dest_ip, u8 *dest_mac);

void ethernet_input(ifnet_t *ifp, mbuf_t *m) {
    if (!ifp || !m || m->m_len < (i32)sizeof(ether_header_t)) {
        m_freem(m);
        return;
    }

    ether_header_t eth;
    __builtin_memcpy(&eth, m->m_data, sizeof(ether_header_t));

    // advance buffer past ethernet header
    m->m_data += sizeof(ether_header_t);
    m->m_len -= sizeof(ether_header_t);
    m->m_pkthdr.len -= sizeof(ether_header_t);

    u16 ethertype = ntohs(eth.ether_type);

    switch (ethertype) {
        case ETHERTYPE_ARP:
            arp_input(ifp, m);
            break;
        case ETHERTYPE_IP:
            ip_input(ifp, m);
            break;
        default:
            m_freem(m);
            break;
    }
}

xiu_error_t ethernet_output(ifnet_t *ifp, mbuf_t *m, struct in_addr dest_ip, u16 ethertype) {
    if (!ifp || !m) return XIU_ERR_INVALID;

    u8 dest_mac[ETHER_ADDR_LEN];
    u32 dest_addr = ntohl(dest_ip.s_addr);

    if (dest_addr == INADDR_BROADCAST ||
        (ifp->if_bcast.s_addr != 0 && dest_ip.s_addr == ifp->if_bcast.s_addr)) {
        __builtin_memset(dest_mac, 0xFF, ETHER_ADDR_LEN);
    } else {
        struct in_addr nexthop = dest_ip;
        if (ifp->if_gateway.s_addr != 0 &&
            (dest_ip.s_addr & ifp->if_netmask.s_addr) != (ifp->if_ip.s_addr & ifp->if_netmask.s_addr)) {
            nexthop = ifp->if_gateway;
        }

        xiu_error_t err = arp_resolve(ifp, nexthop, dest_mac);
        if (err != XIU_SUCCESS) {
            m_freem(m);
            return err;
        }
    }

    // prepend Ethernet header
    mbuf_t *hdr = m_gethdr(MT_HEADER);
    if (!hdr) {
        m_freem(m);
        return XIU_ERR_NOMEM;
    }

    ether_header_t *eth = (ether_header_t *)hdr->m_data;
    __builtin_memcpy(eth->ether_dhost, dest_mac, ETHER_ADDR_LEN);
    __builtin_memcpy(eth->ether_shost, ifp->if_mac, ETHER_ADDR_LEN);
    eth->ether_type = htons(ethertype);

    hdr->m_len = sizeof(ether_header_t);
    hdr->m_pkthdr.len = m->m_pkthdr.len + sizeof(ether_header_t);
    hdr->m_next = m;

    if (ifp->if_output) {
        return ifp->if_output(ifp, hdr, dest_ip);
    }

    m_freem(hdr);
    return XIU_ERR_UNSUPPORTED;
}
