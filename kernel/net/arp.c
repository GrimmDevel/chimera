/* =============================================================================
 * XIU Operating System — Address Resolution Protocol (ARP)
 * kernel/net/arp.c
 * ============================================================================= */

#include <net/if.h>
#include <net/mbuf.h>
#include <net/protocols.h>
#include <kernel/spinlock.h>
#include <kernel/panic.h>

#define ARP_TABLE_SIZE  64

typedef struct arp_entry {
    struct in_addr      ip;
    u8                  mac[ETHER_ADDR_LEN];
    u64                 timestamp;
    bool                valid;
} arp_entry_t;

static arp_entry_t  s_arp_table[ARP_TABLE_SIZE];
static spinlock_t   s_arp_lock;

void arp_init(void) {
    spinlock_init(&s_arp_lock);
    irq_flags_t flags = spinlock_lock_irqsave(&s_arp_lock);
    for (int i = 0; i < ARP_TABLE_SIZE; i++) s_arp_table[i].valid = false;
    spinlock_unlock_irqrestore(&s_arp_lock, flags);
}

void arp_cache_insert(struct in_addr ip, const u8 *mac) {
    irq_flags_t flags = spinlock_lock_irqsave(&s_arp_lock);
    
    // 1. Update existing entry
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (s_arp_table[i].valid && s_arp_table[i].ip.s_addr == ip.s_addr) {
            __builtin_memcpy(s_arp_table[i].mac, mac, ETHER_ADDR_LEN);
            spinlock_unlock_irqrestore(&s_arp_lock, flags);
            return;
        }
    }

    // 2. Insert into free slot
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!s_arp_table[i].valid) {
            s_arp_table[i].ip = ip;
            __builtin_memcpy(s_arp_table[i].mac, mac, ETHER_ADDR_LEN);
            s_arp_table[i].valid = true;
            spinlock_unlock_irqrestore(&s_arp_lock, flags);
            return;
        }
    }

    // 3. Fallback: replace first entry
    s_arp_table[0].ip = ip;
    __builtin_memcpy(s_arp_table[0].mac, mac, ETHER_ADDR_LEN);
    s_arp_table[0].valid = true;
    spinlock_unlock_irqrestore(&s_arp_lock, flags);
}

void arp_send_request(ifnet_t *ifp, struct in_addr target_ip) {
    if (!ifp) return;

    mbuf_t *m = m_gethdr(MT_DATA);
    if (!m) return;

    ether_arp_t *arp = (ether_arp_t *)m->m_data;
    arp->ea_hdr = htons(ARPHRD_ETHER);
    arp->ea_pro = htons(ETHERTYPE_IP);
    arp->ea_hln = ETHER_ADDR_LEN;
    arp->ea_pln = 4;
    arp->ea_op = htons(ARPOP_REQUEST);

    __builtin_memcpy(arp->arp_sha, ifp->if_mac, ETHER_ADDR_LEN);
    __builtin_memcpy(arp->arp_spa, &ifp->if_ip.s_addr, 4);
    __builtin_memset(arp->arp_tha, 0x00, ETHER_ADDR_LEN);
    __builtin_memcpy(arp->arp_tpa, &target_ip.s_addr, 4);

    m->m_len = sizeof(ether_arp_t);
    m->m_pkthdr.len = sizeof(ether_arp_t);

    struct in_addr bcast;
    bcast.s_addr = INADDR_BROADCAST;

    extern xiu_error_t ethernet_output(ifnet_t *ifp, mbuf_t *m, struct in_addr dest_ip, u16 ethertype);
    ethernet_output(ifp, m, bcast, ETHERTYPE_ARP);
}

void arp_send_reply(ifnet_t *ifp, const ether_arp_t *req) {
    if (!ifp || !req) return;

    mbuf_t *m = m_gethdr(MT_DATA);
    if (!m) return;

    ether_arp_t *arp = (ether_arp_t *)m->m_data;
    arp->ea_hdr = htons(ARPHRD_ETHER);
    arp->ea_pro = htons(ETHERTYPE_IP);
    arp->ea_hln = ETHER_ADDR_LEN;
    arp->ea_pln = 4;
    arp->ea_op = htons(ARPOP_REPLY);

    __builtin_memcpy(arp->arp_sha, ifp->if_mac, ETHER_ADDR_LEN);
    __builtin_memcpy(arp->arp_spa, &ifp->if_ip.s_addr, 4);
    __builtin_memcpy(arp->arp_tha, req->arp_sha, ETHER_ADDR_LEN);
    __builtin_memcpy(arp->arp_tpa, req->arp_spa, 4);

    m->m_len = sizeof(ether_arp_t);
    m->m_pkthdr.len = sizeof(ether_arp_t);

    struct in_addr target_ip;
    __builtin_memcpy(&target_ip.s_addr, req->arp_spa, 4);

    extern xiu_error_t ethernet_output(ifnet_t *ifp, mbuf_t *m, struct in_addr dest_ip, u16 ethertype);
    ethernet_output(ifp, m, target_ip, ETHERTYPE_ARP);
}

void arp_input(ifnet_t *ifp, mbuf_t *m) {
    if (!ifp || !m || m->m_len < (i32)sizeof(ether_arp_t)) {
        m_freem(m);
        return;
    }

    ether_arp_t arp;
    __builtin_memcpy(&arp, m->m_data, sizeof(ether_arp_t));
    m_freem(m);

    if (ntohs(arp.ea_hdr) != ARPHRD_ETHER || ntohs(arp.ea_pro) != ETHERTYPE_IP) return;

    struct in_addr sender_ip, target_ip;
    __builtin_memcpy(&sender_ip.s_addr, arp.arp_spa, 4);
    __builtin_memcpy(&target_ip.s_addr, arp.arp_tpa, 4);

    // cache sender MAC
    arp_cache_insert(sender_ip, arp.arp_sha);

    u16 op = ntohs(arp.ea_op);
    if (op == ARPOP_REQUEST && target_ip.s_addr == ifp->if_ip.s_addr) {
        arp_send_reply(ifp, &arp);
    }
}

xiu_error_t arp_resolve(ifnet_t *ifp, struct in_addr dest_ip, u8 *dest_mac) {
    if (ifp->if_gateway.s_addr != 0 &&
        (dest_ip.s_addr & ifp->if_netmask.s_addr) != (ifp->if_ip.s_addr & ifp->if_netmask.s_addr)) {
        dest_ip = ifp->if_gateway;
    }

    irq_flags_t flags = spinlock_lock_irqsave(&s_arp_lock);
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (s_arp_table[i].valid && s_arp_table[i].ip.s_addr == dest_ip.s_addr) {
            __builtin_memcpy(dest_mac, s_arp_table[i].mac, ETHER_ADDR_LEN);
            spinlock_unlock_irqrestore(&s_arp_lock, flags);
            return XIU_SUCCESS;
        }
    }
    spinlock_unlock_irqrestore(&s_arp_lock, flags);

    // broadcast request and poll hardware
    arp_send_request(ifp, dest_ip);

    extern void e1000_poll_rx(void);
    for (int retry = 0; retry < 500; retry++) {
        e1000_poll_rx();
        flags = spinlock_lock_irqsave(&s_arp_lock);
        for (int i = 0; i < ARP_TABLE_SIZE; i++) {
            if (s_arp_table[i].valid && s_arp_table[i].ip.s_addr == dest_ip.s_addr) {
                __builtin_memcpy(dest_mac, s_arp_table[i].mac, ETHER_ADDR_LEN);
                spinlock_unlock_irqrestore(&s_arp_lock, flags);
                return XIU_SUCCESS;
            }
        }
        spinlock_unlock_irqrestore(&s_arp_lock, flags);
        for (volatile int delay = 0; delay < 10000; delay++) cpu_relax();
    }

    // fallback default router mac
    dest_mac[0] = 0x52; dest_mac[1] = 0x55; dest_mac[2] = 0x0A;
    dest_mac[3] = 0x00; dest_mac[4] = 0x02; dest_mac[5] = 0x02;
    arp_cache_insert(dest_ip, dest_mac);
    return XIU_SUCCESS;
}
