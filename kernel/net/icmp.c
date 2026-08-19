/* =============================================================================
 * XIU Operating System — Internet Control Message Protocol (ICMP)
 * kernel/net/icmp.c
 * ============================================================================= */

#include <net/if.h>
#include <net/mbuf.h>
#include <net/protocols.h>
#include <kernel/spinlock.h>

typedef struct ping_listener {
    u16                 id;
    u16                 seq;
    bool                received;
    u64                 recv_time;
} ping_listener_t;

static ping_listener_t s_ping_listeners[16];
static spinlock_t      s_icmp_lock;

void icmp_init(void) {
    spinlock_init(&s_icmp_lock);
    for (int i = 0; i < 16; i++) s_ping_listeners[i].id = 0;
}

extern xiu_error_t ip_output(mbuf_t *m, struct in_addr src_ip, struct in_addr dst_ip, u8 proto);

void icmp_input(ifnet_t *ifp, mbuf_t *m, ip_header_t *ip) {
    if (!ifp || !m || !ip || m->m_len < (i32)sizeof(icmp_header_t)) {
        m_freem(m);
        return;
    }

    icmp_header_t icmp;
    __builtin_memcpy(&icmp, m->m_data, sizeof(icmp_header_t));

    if (icmp.icmp_type == ICMP_ECHO) {
        // auto-reply to Ping Echo Request
        icmp.icmp_type = ICMP_ECHOREPLY;
        icmp.icmp_code = 0;
        icmp.icmp_cksum = 0;

        __builtin_memcpy(m->m_data, &icmp, sizeof(icmp_header_t));

        // compute full ICMP checksum over header + data payload
        u8 temp_buf[1024];
        usize icmp_len = (usize)m->m_pkthdr.len;
        if (icmp_len > sizeof(temp_buf)) icmp_len = sizeof(temp_buf);
        m_copydata(m, 0, (i32)icmp_len, temp_buf);

        icmp_header_t *temp_hdr = (icmp_header_t *)temp_buf;
        temp_hdr->icmp_cksum = 0;
        temp_hdr->icmp_cksum = in_checksum(temp_buf, icmp_len);

        __builtin_memcpy(m->m_data, temp_hdr, sizeof(icmp_header_t));

        // send reply back to sender
        ip_output(m, ip->ip_dst, ip->ip_src, IPPROTO_ICMP);
        return;
    }

    if (icmp.icmp_type == ICMP_ECHOREPLY) {
        u16 id = ntohs(icmp.icmp_hun.echo.id);
        u16 seq = ntohs(icmp.icmp_hun.echo.seq);

        irq_flags_t flags = spinlock_lock_irqsave(&s_icmp_lock);
        for (int i = 0; i < 16; i++) {
            if (s_ping_listeners[i].id == id) {
                s_ping_listeners[i].seq = seq;
                s_ping_listeners[i].received = true;
                break;
            }
        }
        spinlock_unlock_irqrestore(&s_icmp_lock, flags);
    }

    m_freem(m);
}

xiu_error_t icmp_send_echo(struct in_addr dst_ip, u16 id, u16 seq, const void *payload, usize payload_len) {
    usize total_len = sizeof(icmp_header_t) + payload_len;
    mbuf_t *m = m_gethdr(MT_DATA);
    if (!m) return XIU_ERR_NOMEM;

    u8 buf[512];
    if (total_len > sizeof(buf)) return XIU_ERR_INVALID;

    icmp_header_t *icmp = (icmp_header_t *)buf;
    icmp->icmp_type = ICMP_ECHO;
    icmp->icmp_code = 0;
    icmp->icmp_cksum = 0;
    icmp->icmp_hun.echo.id = htons(id);
    icmp->icmp_hun.echo.seq = htons(seq);

    if (payload && payload_len > 0) {
        __builtin_memcpy(buf + sizeof(icmp_header_t), payload, payload_len);
    }

    icmp->icmp_cksum = in_checksum(buf, total_len);

    __builtin_memcpy(m->m_data, buf, total_len);
    m->m_len = (i32)total_len;
    m->m_pkthdr.len = (i32)total_len;

    // register listener
    irq_flags_t flags = spinlock_lock_irqsave(&s_icmp_lock);
    int slot = -1;
    for (int i = 0; i < 16; i++) {
        if (s_ping_listeners[i].id == 0 || s_ping_listeners[i].id == id) {
            slot = i;
            break;
        }
    }
    if (slot != -1) {
        s_ping_listeners[slot].id = id;
        s_ping_listeners[slot].seq = seq;
        s_ping_listeners[slot].received = false;
    }
    spinlock_unlock_irqrestore(&s_icmp_lock, flags);

    struct in_addr src_ip = {0};
    return ip_output(m, src_ip, dst_ip, IPPROTO_ICMP);
}

bool icmp_poll_reply(u16 id, u16 *out_seq) {
    extern void e1000_poll_rx(void);
    e1000_poll_rx();

    irq_flags_t flags = spinlock_lock_irqsave(&s_icmp_lock);
    for (int i = 0; i < 16; i++) {
        if (s_ping_listeners[i].id == id && s_ping_listeners[i].received) {
            if (out_seq) *out_seq = s_ping_listeners[i].seq;
            s_ping_listeners[i].received = false;
            spinlock_unlock_irqrestore(&s_icmp_lock, flags);
            return true;
        }
    }
    spinlock_unlock_irqrestore(&s_icmp_lock, flags);
    return false;
}
