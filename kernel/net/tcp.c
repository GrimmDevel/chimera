/* =============================================================================
 * Chimera Operating System — Transmission Control Protocol (TCP)
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

chimera_error_t tcp_attach(socket_t *so) {
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
            return CHIMERA_SUCCESS;
        }
    }
    spinlock_unlock_irqrestore(&s_tcp_lock, flags);
    return CHIMERA_ERR_NOMEM;
}

extern void so_free(socket_t *so);

void tcp_detach(socket_t *so) {
    if (!so || !so->so_pcb) return;

    irq_flags_t flags = spinlock_lock_irqsave(&s_tcp_lock);
    tcp_pcb_t *pcb = (tcp_pcb_t *)so->so_pcb;
    pcb->so = nullptr;
    pcb->state = TCPS_CLOSED;
    pcb->bound = false;
    so->so_pcb = nullptr;

    // a closing listener takes its queued children (and their pcbs) down
    if (so->so_head == so || so->so_head == nullptr) {
        for (int pass = 0; pass < 2; pass++) {
            socket_t *list = pass ? so->so_q : so->so_q0;
            while (list) {
                socket_t *next = (pass ? list->so_q : list->so_q0);
                tcp_pcb_t *cp = (tcp_pcb_t *)list->so_pcb;
                if (cp) {
                    cp->so = nullptr;
                    cp->state = TCPS_CLOSED;
                    list->so_pcb = nullptr;
                }
                so_free(list);
                list = next;
            }
        }
        so->so_q0 = nullptr;
        so->so_q = nullptr;
        so->so_qlen = 0;
    }
    spinlock_unlock_irqrestore(&s_tcp_lock, flags);
}

// peer address of a connected stream socket (for accept(2))
void tcp_fill_peer(socket_t *so, struct sockaddr_in *sin) {
    if (!so || !sin) return;
    __builtin_memset(sin, 0, sizeof(*sin));
    sin->sin_len = sizeof(*sin);
    sin->sin_family = AF_INET;
    tcp_pcb_t *pcb = (tcp_pcb_t *)so->so_pcb;
    if (pcb) {
        sin->sin_port = htons(pcb->remote_port);
        sin->sin_addr = pcb->remote_ip;
    }
}

chimera_error_t tcp_bind(socket_t *so, struct sockaddr_in *sin) {
    if (!so || !so->so_pcb || !sin) return CHIMERA_ERR_INVALID;

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
            return CHIMERA_ERR_BUSY;
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
    return CHIMERA_SUCCESS;
}

extern chimera_error_t ip_output(mbuf_t *m, struct in_addr src_ip, struct in_addr dst_ip, u8 proto);

// Passive-open: arm a bound stream socket for incoming connections.
socket_t *tcp_child_socket(socket_t *head);

chimera_error_t tcp_listen(socket_t *so) {
    if (!so || !so->so_pcb) return CHIMERA_ERR_INVALID;
    tcp_pcb_t *pcb = (tcp_pcb_t *)so->so_pcb;
    if (!pcb->bound) return CHIMERA_ERR_INVALID; // bind() first

    irq_flags_t flags = spinlock_lock_irqsave(&s_tcp_lock);
    pcb->state = TCPS_LISTEN;
    spinlock_unlock_irqrestore(&s_tcp_lock, flags);
    return CHIMERA_SUCCESS;
}

// allocate a free child PCB and bind it to a freshly created child socket
static tcp_pcb_t *tcp_alloc_child(tcp_pcb_t *listen_pcb, socket_t *listen_so,
                                  struct in_addr local_ip, struct in_addr remote_ip,
                                  u16 remote_port, u32 irs) {
    socket_t *child_so = tcp_child_socket(listen_so);
    if (!child_so) return nullptr;

    irq_flags_t flags = spinlock_lock_irqsave(&s_tcp_lock);
    tcp_pcb_t *child = nullptr;
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (!s_tcp_pcbs[i].so) {
            child = &s_tcp_pcbs[i];
            __builtin_memset(child, 0, sizeof(*child));
            child->so = child_so;
            child->state = TCPS_SYN_RECEIVED;
            // BSD semantics: the connection's local address is the actual
            // destination of the incoming SYN (not the listener's bind-ANY
            // materialized address)
            child->local_ip = local_ip;
            child->local_port = listen_pcb->local_port;
            child->remote_ip = remote_ip;
            child->remote_port = remote_port;
            child->iss = s_iss_counter += 64000;
            child->snd_una = child->iss;
            child->snd_nxt = child->iss;
            child->snd_wnd = 65535;
            child->irs = irs;
            child->rcv_nxt = irs + 1;
            child->rcv_wnd = 65535;
            child->bound = true;
            break;
        }
    }
    spinlock_unlock_irqrestore(&s_tcp_lock, flags);

    if (!child) {
        extern void so_discard_child(socket_t *child);
        so_discard_child(child_so);
        return nullptr;
    }
    child_so->so_pcb = child;
    child_so->so_state |= SS_ISCONNECTED;
    return child;
}

static chimera_error_t tcp_send_packet(tcp_pcb_t *pcb, u8 flags, const void *data, usize len) {
    if (pcb->local_ip.s_addr == 0) {
        ifnet_t *def = if_get_default();
        if (def) pcb->local_ip = def->if_ip;
    }

    mbuf_t *m = m_getcl(MT_DATA);
    if (!m) return CHIMERA_ERR_NOMEM;

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

    if (len > (MCLBYTES - hdr_len - 64)) {
        m_freem(m);
        return CHIMERA_ERR_OVERFLOW;
    }

    if (data && len > 0) {
        __builtin_memcpy(m->m_data + hdr_len, data, len);
    }

    usize total_len = hdr_len + len;
    m->m_len = (i32)total_len;
    m->m_pkthdr.len = (i32)total_len;

    th->th_sum = in_pseudo_checksum(pcb->local_ip.s_addr, pcb->remote_ip.s_addr,
                                    IPPROTO_TCP, (u16)total_len, m->m_data, total_len);

    dprintf("[tcp] Sending %s to %u.%u.%u.%u:%d (sport=%d, seq=0x%x, ack=0x%x, sum=0x%x)\n",
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

chimera_error_t tcp_connect(socket_t *so, struct sockaddr_in *sin) {
    if (!so || !so->so_pcb || !sin) return CHIMERA_ERR_INVALID;

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

    dprintf("[tcp] Connecting to %u.%u.%u.%u:%d (local_port=%d)...\n",
            ((u8 *)&pcb->remote_ip)[0], ((u8 *)&pcb->remote_ip)[1],
            ((u8 *)&pcb->remote_ip)[2], ((u8 *)&pcb->remote_ip)[3],
            pcb->remote_port, pcb->local_port);

    // send SYN
    tcp_send_packet(pcb, TH_SYN, nullptr, 0);

    // poll network for SYN-ACK response
    extern void e1000_poll_rx(void);
    for (int retry = 0; retry < 1000; retry++) {
        e1000_poll_rx();
        if ((retry % 50) == 0)
            kprintf("[TCPDIAG] connect poll retry=%d state=%d\n", retry,
                    pcb->state);
        if (pcb->state == TCPS_ESTABLISHED) {
            dprintf("[tcp] Connection ESTABLISHED to %u.%u.%u.%u:%d!\n",
                    ((u8 *)&pcb->remote_ip)[0], ((u8 *)&pcb->remote_ip)[1],
                    ((u8 *)&pcb->remote_ip)[2], ((u8 *)&pcb->remote_ip)[3],
                    pcb->remote_port);
            so->so_state &= ~SS_ISCONNECTING;
            so->so_state |= SS_ISCONNECTED;
            return CHIMERA_SUCCESS;
        }

        // retransmit SYN if no SYN-ACK received after initial burst
        if ((retry % 250) == 0 && retry > 0) {
            pcb->snd_nxt = pcb->iss;
            dprintf("[tcp] Retransmitting SYN (retry %d)...\n", retry);
            tcp_send_packet(pcb, TH_SYN, nullptr, 0);
        }

        for (volatile int delay = 0; delay < 20000; delay++) cpu_relax();
    }

    dprintf("[tcp] Connection TIMED OUT to %u.%u.%u.%u:%d\n",
            ((u8 *)&pcb->remote_ip)[0], ((u8 *)&pcb->remote_ip)[1],
            ((u8 *)&pcb->remote_ip)[2], ((u8 *)&pcb->remote_ip)[3],
            pcb->remote_port);

    so->so_state &= ~SS_ISCONNECTING;
    pcb->state = TCPS_CLOSED;
    return CHIMERA_ERR_TIMEOUT;
}

chimera_error_t tcp_send(socket_t *so, const void *buf, usize len, int flags) {
    (void)flags;
    if (!so || !so->so_pcb || !(so->so_state & SS_ISCONNECTED)) return CHIMERA_ERR_NOT_CONNECTED;
    if (!buf && len > 0) return CHIMERA_ERR_INVALID;
    if (len == 0) return CHIMERA_SUCCESS;

    tcp_pcb_t *pcb = (tcp_pcb_t *)so->so_pcb;
    const u8 *ptr = (const u8 *)buf;
    usize remaining = len;

    while (remaining > 0) {
        usize chunk = remaining > 1460 ? 1460 : remaining;
        u8 pkt_flags = TH_ACK;
        if (chunk == remaining) pkt_flags |= TH_PUSH;
        chimera_error_t err = tcp_send_packet(pcb, pkt_flags, ptr, chunk);
        if (err != CHIMERA_SUCCESS) return err;
        ptr += chunk;
        remaining -= chunk;
    }
    return CHIMERA_SUCCESS;
}

chimera_error_t tcp_close(socket_t *so) {
    if (!so || !so->so_pcb) return CHIMERA_SUCCESS;

    tcp_pcb_t *pcb = (tcp_pcb_t *)so->so_pcb;
    if (pcb->state == TCPS_ESTABLISHED) {
        pcb->state = TCPS_FIN_WAIT_1;
        tcp_send_packet(pcb, TH_FIN | TH_ACK, nullptr, 0);
    }
    tcp_detach(so);
    return CHIMERA_SUCCESS;
}

void tcp_input(ifnet_t *ifp, mbuf_t *m, ip_header_t *ip) {
    (void)ifp;
    if (!m || !ip || m->m_len < (i32)sizeof(tcp_header_t)) {
        if (m) m_freem(m);
        return;
    }

    // verify TCP checksum over IP pseudo-header + TCP payload
    u16 sum = in_pseudo_checksum(ip->ip_src.s_addr, ip->ip_dst.s_addr,
                                 IPPROTO_TCP, (u16)m->m_len, m->m_data, (usize)m->m_len);
    if (sum != 0) {
        kprintf("[TCPDIAG] checksum FAIL sum=0x%x\n", sum);
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

    kprintf("[TCPDIAG] input sport=%d dport=%d flags=0x%x mlen=%d hlen=%d\n",
            sport, dport, th.th_flags, m->m_len, hlen);

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
    tcp_pcb_t *listener = nullptr;
    // exact 4-tuple match wins; a LISTEN pcb is only the fallback
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        tcp_pcb_t *p = &s_tcp_pcbs[i];
        if (!p->so || !p->bound || p->local_port != dport) continue;
        if (p->state == TCPS_LISTEN) {
            if (!listener) listener = p;
            continue;
        }
        if (p->remote_ip.s_addr == ip->ip_src.s_addr &&
            p->remote_port == sport) {
            pcb = p;
            break;
        }
    }
    if (!pcb && listener && (th.th_flags & TH_SYN) && !(th.th_flags & TH_ACK)) {
        pcb = listener;
    }

    if (!pcb) {
        spinlock_unlock_irqrestore(&s_tcp_lock, flags);
        kprintf("[TCPDIAG] demux MISS dport=%d sport=%d\n", dport, sport);
        m_freem(m);
        return;
    }
    kprintf("[TCPDIAG] demux hit pcb=%p state=%d exact=%d\n", (void *)pcb,
            pcb->state, listener ? 0 : 1);

    // tcp State Transitions
    if (pcb->state == TCPS_LISTEN && (th.th_flags & TH_SYN) && !(th.th_flags & TH_ACK)) {
        // backlog enforcement: a full accept queue silently drops SYNs
        if (pcb->so->so_qlen >= pcb->so->so_qlimit) {
            spinlock_unlock_irqrestore(&s_tcp_lock, flags);
            m_freem(m);
            return;
        }
        kprintf("[TCPDIAG] LISTEN branch qlen=%d qlimit=%d\n",
                pcb->so->so_qlen, pcb->so->so_qlimit);
        socket_t *child_so = pcb->so;
        // release the tcp lock before allocating: tcp_alloc_child takes it
        // itself (and the loopback send below re-enters tcp_input). Single
        // CPU — the listener cannot run concurrently in this window.
        spinlock_unlock_irqrestore(&s_tcp_lock, flags);
        tcp_pcb_t *child =
            tcp_alloc_child(pcb, child_so, ip->ip_dst, ip->ip_src, sport, seq);
        kprintf("[TCPDIAG] alloc child=%p\n", (void *)child);
        if (!child) {
            m_freem(m);
            return;
        }

        // SYN|ACK to complete the three-way handshake
        tcp_send_packet(child, TH_SYN | TH_ACK, nullptr, 0);
        m_freem(m);
        return;
    }

    if (pcb->state == TCPS_SYN_RECEIVED && (th.th_flags & TH_ACK)) {
        kprintf("[TCPDIAG] SYN_RCVD ack=%x snd_nxt=%x\n", ack, pcb->snd_nxt);
        if (ack == pcb->snd_nxt) {
            // handshake complete: move the child from the incomplete queue
            // to the accept queue and report connectivity
            pcb->state = TCPS_ESTABLISHED;
            spinlock_unlock_irqrestore(&s_tcp_lock, flags);

            extern void so_q0_to_q(socket_t *child);
            so_q0_to_q(pcb->so);

            if (m->m_len > 0) {
                // ACK carried data: deliver it in-order
                pcb->rcv_nxt += (u32)m->m_len;
                sbappend(&pcb->so->so_rcv, m);
            } else {
                m_freem(m);
            }
            return;
        }
        spinlock_unlock_irqrestore(&s_tcp_lock, flags);
        m_freem(m);
        return;
    }

    if (pcb->state == TCPS_SYN_SENT && (th.th_flags & (TH_SYN | TH_ACK))) {
        pcb->irs = seq;
        pcb->rcv_nxt = seq + 1;
        pcb->snd_una = ack;
        pcb->state = TCPS_ESTABLISHED;
        spinlock_unlock_irqrestore(&s_tcp_lock, flags);
        kprintf("[TCPDIAG] client ESTABLISHED\n");

        // send ACK
        tcp_send_packet(pcb, TH_ACK, nullptr, 0);
        m_freem(m);
        return;
    }

    if (pcb->state == TCPS_ESTABLISHED) {
        // in-order only: any segment that does not extend rcv_nxt exactly is
        // dropped and left to the peer's retransmission (no SACK/OOO queue)
        if (m->m_len > 0 && seq != pcb->rcv_nxt) {
            kprintf("[TCPDIAG] seq drop seq=%x rcv_nxt=%x\n", seq,
                    pcb->rcv_nxt);
            spinlock_unlock_irqrestore(&s_tcp_lock, flags);
            m_freem(m);
            return;
        }
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
