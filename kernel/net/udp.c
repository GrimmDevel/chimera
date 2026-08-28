/* =============================================================================
 * Chimera Operating System — User Datagram Protocol (UDP)
 * kernel/net/udp.c
 * ============================================================================= */

#include <net/socket.h>
#include <net/protocols.h>
#include <kernel/spinlock.h>
#include <kernel/panic.h>

#define UDP_MAX_SOCKETS 64

typedef struct udp_pcb {
    socket_t           *so;
    struct in_addr      local_ip;
    u16                 local_port;
    struct in_addr      remote_ip;
    u16                 remote_port;
    bool                bound;
} udp_pcb_t;

static udp_pcb_t    s_udp_pcbs[UDP_MAX_SOCKETS];
static spinlock_t   s_udp_lock;
static u16          s_ephemeral_port = 49152;

void udp_init(void) {
    spinlock_init(&s_udp_lock);
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) s_udp_pcbs[i].so = nullptr;
}

chimera_error_t udp_attach(socket_t *so) {
    irq_flags_t flags = spinlock_lock_irqsave(&s_udp_lock);
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!s_udp_pcbs[i].so) {
            s_udp_pcbs[i].so = so;
            s_udp_pcbs[i].local_ip.s_addr = 0;
            s_udp_pcbs[i].local_port = 0;
            s_udp_pcbs[i].remote_ip.s_addr = 0;
            s_udp_pcbs[i].remote_port = 0;
            s_udp_pcbs[i].bound = false;
            so->so_pcb = &s_udp_pcbs[i];
            spinlock_unlock_irqrestore(&s_udp_lock, flags);
            return CHIMERA_SUCCESS;
        }
    }
    spinlock_unlock_irqrestore(&s_udp_lock, flags);
    return CHIMERA_ERR_NOMEM;
}

void udp_detach(socket_t *so) {
    if (!so || !so->so_pcb) return;

    irq_flags_t flags = spinlock_lock_irqsave(&s_udp_lock);
    udp_pcb_t *pcb = (udp_pcb_t *)so->so_pcb;
    pcb->so = nullptr;
    pcb->bound = false;
    so->so_pcb = nullptr;
    spinlock_unlock_irqrestore(&s_udp_lock, flags);
}

chimera_error_t udp_bind(socket_t *so, struct sockaddr_in *sin) {
    if (!so || !so->so_pcb || !sin) return CHIMERA_ERR_INVALID;

    udp_pcb_t *pcb = (udp_pcb_t *)so->so_pcb;
    irq_flags_t flags = spinlock_lock_irqsave(&s_udp_lock);

    u16 port = ntohs(sin->sin_port);
    if (port == 0) {
        port = s_ephemeral_port++;
        if (s_ephemeral_port > 65530) s_ephemeral_port = 49152;
    }

    // check port collision
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (s_udp_pcbs[i].so && s_udp_pcbs[i].bound && s_udp_pcbs[i].local_port == port) {
            spinlock_unlock_irqrestore(&s_udp_lock, flags);
            return CHIMERA_ERR_BUSY;
        }
    }

    pcb->local_port = port;
    pcb->local_ip = sin->sin_addr;
    pcb->bound = true;

    spinlock_unlock_irqrestore(&s_udp_lock, flags);
    return CHIMERA_SUCCESS;
}

extern chimera_error_t ip_output(mbuf_t *m, struct in_addr src_ip, struct in_addr dst_ip, u8 proto);

chimera_error_t udp_sendto(socket_t *so, const void *buf, usize len, struct sockaddr_in *dest) {
    if (!so || !so->so_pcb || !dest || len > 1472) return CHIMERA_ERR_INVALID;

    udp_pcb_t *pcb = (udp_pcb_t *)so->so_pcb;
    if (!pcb->bound) {
        struct sockaddr_in auto_sin = {0};
        auto_sin.sin_family = AF_INET;
        udp_bind(so, &auto_sin);
    }

    mbuf_t *m = m_getcl(MT_DATA);
    if (!m) return CHIMERA_ERR_NOMEM;

    udp_header_t *uh = (udp_header_t *)m->m_data;
    uh->uh_sport = htons(pcb->local_port);
    uh->uh_dport = dest->sin_port;
    uh->uh_ulen = htons((u16)(sizeof(udp_header_t) + len));
    uh->uh_sum = 0;

    if (len > 0 && buf) {
        __builtin_memcpy(m->m_data + sizeof(udp_header_t), buf, len);
    }

    struct in_addr src_ip = pcb->local_ip;
    if (src_ip.s_addr == 0) {
        ifnet_t *def_if = if_get_default();
        if (def_if) src_ip = def_if->if_ip;
    }

    usize total_len = sizeof(udp_header_t) + len;
    m->m_len = (i32)total_len;
    m->m_pkthdr.len = (i32)total_len;

    uh->uh_sum = 0; // rfc 768: 0 means no checksum

    return ip_output(m, src_ip, dest->sin_addr, IPPROTO_UDP);
}

void udp_input(ifnet_t *ifp, mbuf_t *m, ip_header_t *ip) {
    (void)ifp;
    if (!m || !ip || m->m_len < (i32)sizeof(udp_header_t)) {
        m_freem(m);
        return;
    }

    udp_header_t uh;
    __builtin_memcpy(&uh, m->m_data, sizeof(udp_header_t));

    u16 dport = ntohs(uh.uh_dport);
    u16 ulen = ntohs(uh.uh_ulen);

    if (ulen < sizeof(udp_header_t) || m->m_len < (i32)ulen) {
        m_freem(m);
        return;
    }

    // advance buffer past UDP header
    m->m_data += sizeof(udp_header_t);
    m->m_len -= sizeof(udp_header_t);
    m->m_pkthdr.len -= sizeof(udp_header_t);

    irq_flags_t flags = spinlock_lock_irqsave(&s_udp_lock);
    socket_t *target_so = nullptr;
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (s_udp_pcbs[i].so && s_udp_pcbs[i].bound && s_udp_pcbs[i].local_port == dport) {
            target_so = s_udp_pcbs[i].so;
            break;
        }
    }
    spinlock_unlock_irqrestore(&s_udp_lock, flags);

    if (target_so) {
        sbappend(&target_so->so_rcv, m);
    } else {
        m_freem(m);
    }
}
