/* =============================================================================
 * XIU Operating System — BSD Socket Layer (uipc_socket)
 * kernel/net/uipc_socket.c
 * ============================================================================= */

#include <net/socket.h>
#include <net/protocols.h>
#include <kernel/fileproc.h>
#include <kernel/spinlock.h>
#include <kernel/panic.h>

#define MAX_SOCKETS 128

static socket_t     s_socket_pool[MAX_SOCKETS];
static spinlock_t   s_so_pool_lock;

void uipc_socket_init(void) {
    spinlock_init(&s_so_pool_lock);
    for (int i = 0; i < MAX_SOCKETS; i++) {
        s_socket_pool[i].so_signature = 0;
    }
}

void sbappend(struct sockbuf *sb, mbuf_t *m) {
    if (!sb || !m) return;

    irq_flags_t flags = spinlock_lock_irqsave(&sb->sb_lock);
    if (!sb->sb_mb) {
        sb->sb_mb = m;
    } else {
        sb->sb_last->m_nextpkt = m;
    }
    sb->sb_last = m;
    m->m_nextpkt = nullptr;

    mbuf_t *curr = m;
    while (curr) {
        sb->sb_cc += (u32)curr->m_len;
        curr = curr->m_next;
    }
    spinlock_unlock_irqrestore(&sb->sb_lock, flags);
}

void sbflush(struct sockbuf *sb) {
    if (!sb) return;

    irq_flags_t flags = spinlock_lock_irqsave(&sb->sb_lock);
    mbuf_t *m = sb->sb_mb;
    sb->sb_mb = nullptr;
    sb->sb_last = nullptr;
    sb->sb_cc = 0;
    spinlock_unlock_irqrestore(&sb->sb_lock, flags);

    while (m) {
        mbuf_t *nextpkt = m->m_nextpkt;
        m_freem(m);
        m = nextpkt;
    }
}

extern xiu_error_t udp_attach(socket_t *so);
extern void udp_detach(socket_t *so);
extern xiu_error_t udp_bind(socket_t *so, struct sockaddr_in *sin);
extern xiu_error_t udp_sendto(socket_t *so, const void *buf, usize len, struct sockaddr_in *dest);

extern xiu_error_t tcp_attach(socket_t *so);
extern void tcp_detach(socket_t *so);
extern xiu_error_t tcp_bind(socket_t *so, struct sockaddr_in *sin);
extern xiu_error_t tcp_connect(socket_t *so, struct sockaddr_in *sin);
extern xiu_error_t tcp_send(socket_t *so, const void *buf, usize len, int flags);
extern xiu_error_t tcp_close(socket_t *so);

xiu_error_t socreate(int dom, socket_t **aso, int type, int proto) {
    if (!aso) return XIU_ERR_INVALID;
    if (dom != AF_INET && dom != AF_UNIX) return XIU_ERR_NOT_SUPPORTED;

    irq_flags_t flags = spinlock_lock_irqsave(&s_so_pool_lock);
    socket_t *so = nullptr;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (s_socket_pool[i].so_signature != XIU_SOCKET_MAGIC) {
            so = &s_socket_pool[i];
            break;
        }
    }
    if (!so) {
        spinlock_unlock_irqrestore(&s_so_pool_lock, flags);
        return XIU_ERR_NOMEM;
    }

    __builtin_memset(so, 0, sizeof(*so));
    so->so_signature = XIU_SOCKET_MAGIC;
    so->so_type = (i16)type;
    so->so_refcount = 1;
    spinlock_init(&so->so_lock);
    spinlock_init(&so->so_snd.sb_lock);
    spinlock_init(&so->so_rcv.sb_lock);
    so->so_snd.sb_hiwat = SB_MAX;
    so->so_rcv.sb_hiwat = SB_MAX;

    spinlock_unlock_irqrestore(&s_so_pool_lock, flags);

    xiu_error_t err = XIU_SUCCESS;
    if (type == SOCK_DGRAM || proto == IPPROTO_UDP) {
        err = udp_attach(so);
    } else if (type == SOCK_STREAM || proto == IPPROTO_TCP) {
        err = tcp_attach(so);
    }

    if (err != XIU_SUCCESS) {
        so->so_signature = 0;
        return err;
    }

    *aso = so;
    return XIU_SUCCESS;
}

xiu_error_t sobind(socket_t *so, struct sockaddr *nam) {
    if (!so || !nam) return XIU_ERR_INVALID;
    if (nam->sa_family != AF_INET) return XIU_ERR_NOT_SUPPORTED;

    struct sockaddr_in *sin = (struct sockaddr_in *)nam;
    if (so->so_type == SOCK_DGRAM) {
        return udp_bind(so, sin);
    } else if (so->so_type == SOCK_STREAM) {
        return tcp_bind(so, sin);
    }
    return XIU_ERR_NOT_SUPPORTED;
}

xiu_error_t soconnect(socket_t *so, struct sockaddr *nam) {
    if (!so || !nam) return XIU_ERR_INVALID;
    if (nam->sa_family != AF_INET) return XIU_ERR_NOT_SUPPORTED;

    struct sockaddr_in *sin = (struct sockaddr_in *)nam;
    if (so->so_type == SOCK_STREAM) {
        return tcp_connect(so, sin);
    }
    return XIU_ERR_NOT_SUPPORTED;
}

xiu_error_t solisten(socket_t *so, int backlog) {
    if (!so) return XIU_ERR_INVALID;
    so->so_qlimit = (i16)(backlog > 0 ? backlog : 5);
    return XIU_SUCCESS;
}

xiu_error_t soaccept(socket_t *so, struct sockaddr **nam, socket_t **new_so) {
    (void)nam;
    if (!so || !new_so) return XIU_ERR_INVALID;
    return XIU_ERR_NOT_SUPPORTED;
}

xiu_error_t sosend(socket_t *so, struct sockaddr *addr, const void *buf, usize len, int flags) {
    if (!so || !buf) return XIU_ERR_INVALID;

    if (so->so_type == SOCK_DGRAM) {
        if (!addr) return XIU_ERR_INVALID;
        return udp_sendto(so, buf, len, (struct sockaddr_in *)addr);
    } else if (so->so_type == SOCK_STREAM) {
        return tcp_send(so, buf, len, flags);
    }
    return XIU_ERR_NOT_SUPPORTED;
}

extern void e1000_poll_rx(void);

xiu_error_t soreceive(socket_t *so, struct sockaddr **addr, void *buf, usize len, usize *out_len, int flags) {
    (void)flags;
    (void)addr;
    if (!so || !buf || !out_len) return XIU_ERR_INVALID;

    // poll network if receive buffer is empty
    for (int retry = 0; retry < 500 && so->so_rcv.sb_cc == 0; retry++) {
        e1000_poll_rx();
        if (so->so_rcv.sb_cc > 0) break;
        if (so->so_state & SS_CANTRCVMORE) {
            *out_len = 0;
            return XIU_SUCCESS; // eof
        }
        for (volatile int delay = 0; delay < 10000; delay++) cpu_relax();
    }

    irq_flags_t f = spinlock_lock_irqsave(&so->so_rcv.sb_lock);
    if (!so->so_rcv.sb_mb) {
        spinlock_unlock_irqrestore(&so->so_rcv.sb_lock, f);
        *out_len = 0;
        return (so->so_state & SS_CANTRCVMORE) ? XIU_SUCCESS : XIU_ERR_WOULDBLOCK;
    }

    mbuf_t *m = so->so_rcv.sb_mb;
    usize copied = 0;

    while (m && copied < len) {
        usize chunk = (usize)m->m_len;
        if (copied + chunk > len) chunk = len - copied;
        __builtin_memcpy((u8 *)buf + copied, m->m_data, chunk);

        m->m_data += chunk;
        m->m_len -= (i32)chunk;
        so->so_rcv.sb_cc -= (u32)chunk;
        copied += chunk;

        if (m->m_len == 0) {
            mbuf_t *next = m->m_next ? m->m_next : m->m_nextpkt;
            m_free(m);
            so->so_rcv.sb_mb = next;
            m = next;
        }
    }

    spinlock_unlock_irqrestore(&so->so_rcv.sb_lock, f);
    *out_len = copied;
    return XIU_SUCCESS;
}

xiu_error_t soshutdown(socket_t *so, int how) {
    if (!so) return XIU_ERR_INVALID;
    if (how == SHUT_RD || how == SHUT_RDWR) so->so_state |= SS_CANTRCVMORE;
    if (how == SHUT_WR || how == SHUT_RDWR) so->so_state |= SS_CANTSENDMORE;
    return XIU_SUCCESS;
}

xiu_error_t soclose(socket_t *so) {
    if (!so || so->so_signature != XIU_SOCKET_MAGIC) return XIU_ERR_INVALID;

    if (so->so_type == SOCK_DGRAM) {
        udp_detach(so);
    } else if (so->so_type == SOCK_STREAM) {
        tcp_close(so);
    }

    sbflush(&so->so_rcv);
    sbflush(&so->so_snd);

    irq_flags_t flags = spinlock_lock_irqsave(&s_so_pool_lock);
    so->so_signature = 0;
    spinlock_unlock_irqrestore(&s_so_pool_lock, flags);
    return XIU_SUCCESS;
}

xiu_error_t sosetopt(socket_t *so, int level, int optname, const void *optval, usize optlen) {
    if (!so || so->so_signature != XIU_SOCKET_MAGIC) return XIU_ERR_INVALID;
    if (!optval || optlen == 0) return XIU_ERR_INVALID;

    if (level == SOL_SOCKET) {
        switch (optname) {
        case SO_REUSEADDR:
        case SO_KEEPALIVE:
        case SO_DONTROUTE:
        case SO_BROADCAST:
        case SO_DEBUG: {
            if (optlen < sizeof(int)) return XIU_ERR_INVALID;
            int val = *(const int *)optval;
            irq_flags_t flags = spinlock_lock_irqsave(&so->so_lock);
            if (val) so->so_options |= (u32)optname;
            else so->so_options &= ~(u32)optname;
            spinlock_unlock_irqrestore(&so->so_lock, flags);
            return XIU_SUCCESS;
        }
        case SO_RCVTIMEO: {
            if (optlen < sizeof(u32)) return XIU_ERR_INVALID;
            so->so_rcvtimeo = *(const u32 *)optval;
            return XIU_SUCCESS;
        }
        case SO_SNDTIMEO: {
            if (optlen < sizeof(u32)) return XIU_ERR_INVALID;
            so->so_sndtimeo = *(const u32 *)optval;
            return XIU_SUCCESS;
        }
        case SO_RCVBUF: {
            if (optlen < sizeof(int)) return XIU_ERR_INVALID;
            int sz = *(const int *)optval;
            if (sz > 0 && sz <= SB_MAX) so->so_rcv.sb_hiwat = (u32)sz;
            return XIU_SUCCESS;
        }
        case SO_SNDBUF: {
            if (optlen < sizeof(int)) return XIU_ERR_INVALID;
            int sz = *(const int *)optval;
            if (sz > 0 && sz <= SB_MAX) so->so_snd.sb_hiwat = (u32)sz;
            return XIU_SUCCESS;
        }
        default:
            return XIU_ERR_NOTSUP;
        }
    } else if (level == IPPROTO_TCP) {
        if (optname == 1) { // TCP_NODELAY
            return XIU_SUCCESS;
        }
        return XIU_ERR_NOTSUP;
    }
    return XIU_ERR_NOTSUP;
}

xiu_error_t sogetopt(socket_t *so, int level, int optname, void *optval, usize *optlen) {
    if (!so || so->so_signature != XIU_SOCKET_MAGIC) return XIU_ERR_INVALID;
    if (!optval || !optlen || *optlen == 0) return XIU_ERR_INVALID;

    if (level == SOL_SOCKET) {
        switch (optname) {
        case SO_REUSEADDR:
        case SO_KEEPALIVE:
        case SO_DONTROUTE:
        case SO_BROADCAST:
        case SO_DEBUG: {
            int val = (so->so_options & (u32)optname) ? 1 : 0;
            usize to_copy = *optlen < sizeof(int) ? *optlen : sizeof(int);
            __builtin_memcpy(optval, &val, to_copy);
            *optlen = to_copy;
            return XIU_SUCCESS;
        }
        case SO_TYPE: {
            int val = (int)so->so_type;
            usize to_copy = *optlen < sizeof(int) ? *optlen : sizeof(int);
            __builtin_memcpy(optval, &val, to_copy);
            *optlen = to_copy;
            return XIU_SUCCESS;
        }
        case SO_ERROR: {
            int err = (int)so->so_error;
            so->so_error = 0;
            usize to_copy = *optlen < sizeof(int) ? *optlen : sizeof(int);
            __builtin_memcpy(optval, &err, to_copy);
            *optlen = to_copy;
            return XIU_SUCCESS;
        }
        case SO_RCVTIMEO: {
            u32 val = so->so_rcvtimeo;
            usize to_copy = *optlen < sizeof(u32) ? *optlen : sizeof(u32);
            __builtin_memcpy(optval, &val, to_copy);
            *optlen = to_copy;
            return XIU_SUCCESS;
        }
        case SO_SNDTIMEO: {
            u32 val = so->so_sndtimeo;
            usize to_copy = *optlen < sizeof(u32) ? *optlen : sizeof(u32);
            __builtin_memcpy(optval, &val, to_copy);
            *optlen = to_copy;
            return XIU_SUCCESS;
        }
        case SO_RCVBUF: {
            int val = (int)so->so_rcv.sb_hiwat;
            usize to_copy = *optlen < sizeof(int) ? *optlen : sizeof(int);
            __builtin_memcpy(optval, &val, to_copy);
            *optlen = to_copy;
            return XIU_SUCCESS;
        }
        case SO_SNDBUF: {
            int val = (int)so->so_snd.sb_hiwat;
            usize to_copy = *optlen < sizeof(int) ? *optlen : sizeof(int);
            __builtin_memcpy(optval, &val, to_copy);
            *optlen = to_copy;
            return XIU_SUCCESS;
        }
        default:
            return XIU_ERR_NOTSUP;
        }
    }
    return XIU_ERR_NOTSUP;
}

i16 sopoll(socket_t *so, i16 events) {
    if (!so || so->so_signature != XIU_SOCKET_MAGIC) return 0x0020; // POLLNVAL
    i16 revents = 0;

    irq_flags_t flags = spinlock_lock_irqsave(&so->so_lock);

    if (events & 0x0001) { // POLLIN
        if (so->so_rcv.sb_cc > 0 ||
            (so->so_state & SS_CANTRCVMORE) ||
            so->so_qlen > 0 ||
            so->so_error != 0) {
            revents |= 0x0001;
        }
    }

    if (events & 0x0004) { // POLLOUT
        if (so->so_type == SOCK_DGRAM) {
            if (!(so->so_state & SS_CANTSENDMORE)) {
                revents |= 0x0004;
            }
        } else if (so->so_type == SOCK_STREAM) {
            if ((so->so_state & SS_ISCONNECTED) && (so->so_snd.sb_cc < so->so_snd.sb_hiwat)) {
                revents |= 0x0004;
            }
        }
    }

    if (so->so_error != 0) {
        revents |= 0x0008; // POLLERR
    }

    if ((so->so_state & (SS_CANTRCVMORE | SS_CANTSENDMORE)) == (SS_CANTRCVMORE | SS_CANTSENDMORE)) {
        revents |= 0x0010; // POLLHUP
    }

    spinlock_unlock_irqrestore(&so->so_lock, flags);
    return revents;
}

