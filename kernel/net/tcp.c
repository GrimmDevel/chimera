/* =============================================================================
 * XIU Operating System — Transmission Control Protocol (TCP)
 * kernel/net/tcp.c
 * ============================================================================= */

#include <net/socket.h>
#include <net/protocols.h>
#include <kernel/spinlock.h>
#include <kernel/panic.h>

#define TCP_MAX_SOCKETS 64

// tcp States
typedef enum tcp_state {
    TCPS_CLOSED = 0,
    TCPS_LISTEN = 1,
    TCPS_SYN_SENT = 2,
    TCPS_SYN_RECEIVED = 3,
    TCPS_ESTABLISHED = 4,
    TCPS_CLOSE_WAIT = 5,
    TCPS_FIN_WAIT_1 = 6,
    TCPS_CLOSING = 7,
    TCPS_LAST_ACK = 8,
    TCPS_FIN_WAIT_2 = 9,
    TCPS_TIME_WAIT = 10
} tcp_state_t;

typedef struct tcp_pcb {
    socket_t           *so;
    tcp_state_t         state;
    struct in_addr      local_ip;
    u16                 local_port;
    struct in_addr      remote_ip;
    u16                 remote_port;

    u32                 iss;            // initial send sequence
    u32                 snd_una;        // send unacknowledged
    u32                 snd_nxt;        // send next
    u32                 snd_wnd;        // send window

    u32                 irs;            // initial receive sequence
    u32                 rcv_nxt;        // receive next
    u32                 rcv_wnd;        // receive window

    bool                bound;
} tcp_pcb_t;

static tcp_pcb_t    s_tcp_pcbs[TCP_MAX_SOCKETS];
static spinlock_t   s_tcp_lock;
static u16          s_ephemeral_tcp_port = 49152;
static u32          s_iss_counter = 0x12345678;

void tcp_init(void) {
    spinlock_init(&s_tcp_lock);
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) s_tcp_pcbs[i].so = nullptr;
}

xiu_error_t tcp_attach(socket_t *so) {
    irq_flags_t flags = spinlock_lock_irqsave(&s_tcp_lock);
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (!s_tcp_pcbs[i].so) {
            s_tcp_pcbs[i].so = so;
            s_tcp_pcbs[i].state = TCPS_CLOSED;
            s_tcp_pcbs[i].local_ip.s_addr = 0;
            s_tcp_pcbs[i].local_port = 0;
            s_tcp_pcbs[i].remote_ip.s_addr = 0;
            s_tcp_pcbs[i].remote_port = 0;
            s_tcp_pcbs[i].iss = s_iss_counter += 64000;
            s_tcp_pcbs[i].snd_una = s_tcp_pcbs[i].iss;
            s_tcp_pcbs[i].snd_nxt = s_tcp_pcbs[i].iss;
            s_tcp_pcbs[i].snd_wnd = 65535;
            s_tcp_pcbs[i].rcv_wnd = 65535;
            s_tcp_pcbs[i].bound = false;
            so->so_pcb = &s_tcp_pcbs[i];
            spinlock_unlock_irqrestore(&s_tcp_lock, flags);
            return XIU_SUCCESS;
        }
    }
    spinlock_unlock_irqrestore(&s_tcp_lock, flags);
    return XIU_ERR_NOMEM;
}

void tcp_detach(socket_t *so) {
    if (!so || !so->so_pcb) return;

    irq_flags_t flags = spinlock_lock_irqsave(&s_tcp_lock);
    tcp_pcb_t *pcb = (tcp_pcb_t *)so->so_pcb;
    pcb->so = nullptr;
    pcb->state = TCPS_CLOSED;
    pcb->bound = false;
    so->so_pcb = nullptr;
    spinlock_unlock_irqrestore(&s_tcp_lock, flags);
}

xiu_error_t tcp_bind(socket_t *so, struct sockaddr_in *sin) {
    if (!so || !so->so_pcb || !sin) return XIU_ERR_INVALID;

    tcp_pcb_t *pcb = (tcp_pcb_t *)so->so_pcb;
    irq_flags_t flags = spinlock_lock_irqsave(&s_tcp_lock);

    u16 port = ntohs(sin->sin_port);
    if (port == 0) {
        port = s_ephemeral_tcp_port++;
        if (s_ephemeral_tcp_port > 65530) s_ephemeral_tcp_port = 49152;
    }

    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (s_tcp_pcbs[i].so && s_tcp_pcbs[i].bound && s_tcp_pcbs[i].local_port == port) {
            spinlock_unlock_irqrestore(&s_tcp_lock, flags);
            return XIU_ERR_BUSY;
        }
    }

    pcb->local_port = port;
    if (sin->sin_addr.s_addr == 0) {
        ifnet_t *def = if_get_default();
        if (def) pcb->local_ip = def->if_ip;
        else pcb->local_ip.s_addr = 0;
    } else {
        pcb->local_ip = sin->sin_addr;
    }
    pcb->bound = true;

    spinlock_unlock_irqrestore(&s_tcp_lock, flags);
    return XIU_SUCCESS;
}

extern xiu_error_t ip_output(mbuf_t *m, struct in_addr src_ip, struct in_addr dst_ip, u8 proto);

static xiu_error_t tcp_send_packet(tcp_pcb_t *pcb, u8 flags, const void *data, usize len) {
    if (pcb->local_ip.s_addr == 0) {
        ifnet_t *def = if_get_default();
        if (def) pcb->local_ip = def->if_ip;
    }

    mbuf_t *m = m_getcl(MT_DATA);
    if (!m) return XIU_ERR_NOMEM;

    tcp_header_t *th = (tcp_header_t *)m->m_data;
    th->th_sport = htons(pcb->local_port);
    th->th_dport = htons(pcb->remote_port);
    th->th_seq = htonl(pcb->snd_nxt);
    th->th_ack = htonl(pcb->rcv_nxt);
    th->th_flags = flags;
    th->th_win = htons((u16)pcb->rcv_wnd);
    th->th_sum = 0;
    th->th_urp = 0;

    usize hdr_len = sizeof(tcp_header_t);

    if (flags & TH_SYN) {
        u8 *opts = m->m_data + sizeof(tcp_header_t);
        opts[0] = 2;
        opts[1] = 4;
        opts[2] = (1460 >> 8) & 0xFF;
        opts[3] = 1460 & 0xFF;
        hdr_len += 4;
    }

    th->th_off_x2 = (u8)((hdr_len / 4) << 4);

    if (data && len > 0) {
        __builtin_memcpy(m->m_data + hdr_len, data, len);
    }

    usize total_len = hdr_len + len;
    m->m_len = (i32)total_len;
    m->m_pkthdr.len = (i32)total_len;

    th->th_sum = in_pseudo_checksum(pcb->local_ip.s_addr, pcb->remote_ip.s_addr,
                                    IPPROTO_TCP, (u16)total_len, m->m_data, total_len);

    kprintf("[tcp] Sending %s to %u.%u.%u.%u:%d (sport=%d, seq=0x%x, ack=0x%x, sum=0x%x)\n",
            (flags & TH_SYN) ? "SYN" : ((flags & TH_FIN) ? "FIN" : "ACK"),
            ((u8 *)&pcb->remote_ip)[0], ((u8 *)&pcb->remote_ip)[1],
            ((u8 *)&pcb->remote_ip)[2], ((u8 *)&pcb->remote_ip)[3],
            pcb->remote_port, pcb->local_port, pcb->snd_nxt, pcb->rcv_nxt, th->th_sum);

    if (flags & (TH_SYN | TH_FIN)) {
        pcb->snd_nxt++;
    } else {
        pcb->snd_nxt += (u32)len;
    }

    return ip_output(m, pcb->local_ip, pcb->remote_ip, IPPROTO_TCP);
}

xiu_error_t tcp_connect(socket_t *so, struct sockaddr_in *sin) {
    if (!so || !so->so_pcb || !sin) return XIU_ERR_INVALID;

    tcp_pcb_t *pcb = (tcp_pcb_t *)so->so_pcb;
    if (!pcb->bound) {
        struct sockaddr_in auto_sin = {0};
        auto_sin.sin_family = AF_INET;
        tcp_bind(so, &auto_sin);
    }

    pcb->remote_ip = sin->sin_addr;
    pcb->remote_port = ntohs(sin->sin_port);
    pcb->state = TCPS_SYN_SENT;
    so->so_state |= SS_ISCONNECTING;

    kprintf("[tcp] Connecting to %u.%u.%u.%u:%d (local_port=%d)...\n",
            ((u8 *)&pcb->remote_ip)[0], ((u8 *)&pcb->remote_ip)[1],
            ((u8 *)&pcb->remote_ip)[2], ((u8 *)&pcb->remote_ip)[3],
            pcb->remote_port, pcb->local_port);

    // send SYN
    tcp_send_packet(pcb, TH_SYN, nullptr, 0);

    // poll network for SYN-ACK response
    extern void e1000_poll_rx(void);
    for (int retry = 0; retry < 1000; retry++) {
        e1000_poll_rx();
        if (pcb->state == TCPS_ESTABLISHED) {
            kprintf("[tcp] Connection ESTABLISHED to %u.%u.%u.%u:%d!\n",
                    ((u8 *)&pcb->remote_ip)[0], ((u8 *)&pcb->remote_ip)[1],
                    ((u8 *)&pcb->remote_ip)[2], ((u8 *)&pcb->remote_ip)[3],
                    pcb->remote_port);
            so->so_state &= ~SS_ISCONNECTING;
            so->so_state |= SS_ISCONNECTED;
            return XIU_SUCCESS;
        }

        // retransmit SYN if no SYN-ACK received after initial burst
        if ((retry % 250) == 0 && retry > 0) {
            pcb->snd_nxt = pcb->iss;
            kprintf("[tcp] Retransmitting SYN (retry %d)...\n", retry);
            tcp_send_packet(pcb, TH_SYN, nullptr, 0);
        }

        for (volatile int delay = 0; delay < 20000; delay++) cpu_relax();
    }

    kprintf("[tcp] Connection TIMED OUT to %u.%u.%u.%u:%d\n",
            ((u8 *)&pcb->remote_ip)[0], ((u8 *)&pcb->remote_ip)[1],
            ((u8 *)&pcb->remote_ip)[2], ((u8 *)&pcb->remote_ip)[3],
            pcb->remote_port);

    so->so_state &= ~SS_ISCONNECTING;
    pcb->state = TCPS_CLOSED;
    return XIU_ERR_TIMEOUT;
}

xiu_error_t tcp_send(socket_t *so, const void *buf, usize len, int flags) {
    (void)flags;
    if (!so || !so->so_pcb || !(so->so_state & SS_ISCONNECTED)) return XIU_ERR_NOT_CONNECTED;

    tcp_pcb_t *pcb = (tcp_pcb_t *)so->so_pcb;
    return tcp_send_packet(pcb, TH_ACK | TH_PUSH, buf, len);
}

xiu_error_t tcp_close(socket_t *so) {
    if (!so || !so->so_pcb) return XIU_SUCCESS;

    tcp_pcb_t *pcb = (tcp_pcb_t *)so->so_pcb;
    if (pcb->state == TCPS_ESTABLISHED) {
        pcb->state = TCPS_FIN_WAIT_1;
        tcp_send_packet(pcb, TH_FIN | TH_ACK, nullptr, 0);
    }
    tcp_detach(so);
    return XIU_SUCCESS;
}

void tcp_input(ifnet_t *ifp, mbuf_t *m, ip_header_t *ip) {
    (void)ifp;
    if (!m || !ip || m->m_len < (i32)sizeof(tcp_header_t)) {
        m_freem(m);
        return;
    }

    tcp_header_t th;
    __builtin_memcpy(&th, m->m_data, sizeof(tcp_header_t));

    u16 sport = ntohs(th.th_sport);
    u16 dport = ntohs(th.th_dport);
    u32 seq = ntohl(th.th_seq);
    u32 ack = ntohl(th.th_ack);
    u8 hlen = (u8)(TH_OFF(&th) * 4);

    kprintf("[tcp] Received packet: sport=%d dport=%d flags=0x%x len=%d\n",
            sport, dport, th.th_flags, m->m_len - hlen);

    if (hlen < sizeof(tcp_header_t) || m->m_len < (i32)hlen) {
        m_freem(m);
        return;
    }

    // advance buffer past TCP header
    m->m_data += hlen;
    m->m_len -= hlen;
    m->m_pkthdr.len -= hlen;

    irq_flags_t flags = spinlock_lock_irqsave(&s_tcp_lock);
    tcp_pcb_t *pcb = nullptr;
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (s_tcp_pcbs[i].so && s_tcp_pcbs[i].bound && s_tcp_pcbs[i].local_port == dport) {
            pcb = &s_tcp_pcbs[i];
            break;
        }
    }

    if (!pcb) {
        spinlock_unlock_irqrestore(&s_tcp_lock, flags);
        m_freem(m);
        return;
    }

    // tcp State Transitions
    if (pcb->state == TCPS_SYN_SENT && (th.th_flags & (TH_SYN | TH_ACK))) {
        pcb->irs = seq;
        pcb->rcv_nxt = seq + 1;
        pcb->snd_una = ack;
        pcb->state = TCPS_ESTABLISHED;
        spinlock_unlock_irqrestore(&s_tcp_lock, flags);

        // send ACK
        tcp_send_packet(pcb, TH_ACK, nullptr, 0);
        m_freem(m);
        return;
    }

    if (pcb->state == TCPS_ESTABLISHED) {
        if (m->m_len > 0) {
            pcb->rcv_nxt += (u32)m->m_len;
            sbappend(&pcb->so->so_rcv, m);
            spinlock_unlock_irqrestore(&s_tcp_lock, flags);

            // send ACK for received data
            tcp_send_packet(pcb, TH_ACK, nullptr, 0);
            return;
        }

        if (th.th_flags & TH_FIN) {
            pcb->rcv_nxt++;
            pcb->state = TCPS_CLOSE_WAIT;
            pcb->so->so_state |= SS_CANTRCVMORE;
            spinlock_unlock_irqrestore(&s_tcp_lock, flags);

            // ack FIN
            tcp_send_packet(pcb, TH_ACK, nullptr, 0);
            m_freem(m);
            return;
        }
    }

    spinlock_unlock_irqrestore(&s_tcp_lock, flags);
    m_freem(m);
}
