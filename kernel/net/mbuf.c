/* =============================================================================
 * Chimera Operating System — Memory Buffer (mbuf) Implementation
 * kernel/net/mbuf.c
 * ============================================================================= */

#include <net/mbuf.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>

#define MBUF_POOL_SIZE  512

static mbuf_t           s_mbuf_pool[MBUF_POOL_SIZE];
static u8               s_cluster_pool[MBUF_POOL_SIZE][MCLBYTES];
static mbuf_t          *s_mbuf_freelist = nullptr;
static spinlock_t       s_mbuf_lock;

void mbuf_init(void) {
    spinlock_init(&s_mbuf_lock);
    
    irq_flags_t flags = spinlock_lock_irqsave(&s_mbuf_lock);
    s_mbuf_freelist = nullptr;
    
    for (int i = 0; i < MBUF_POOL_SIZE; i++) {
        mbuf_t *m = &s_mbuf_pool[i];
        __builtin_memset(m, 0, sizeof(mbuf_t));
        m->m_type = MT_FREE;
        m->m_next = s_mbuf_freelist;
        s_mbuf_freelist = m;
    }
    spinlock_unlock_irqrestore(&s_mbuf_lock, flags);
}

mbuf_t *m_get(u16 type) {
    irq_flags_t flags = spinlock_lock_irqsave(&s_mbuf_lock);
    if (!s_mbuf_freelist) {
        spinlock_unlock_irqrestore(&s_mbuf_lock, flags);
        return nullptr;
    }

    mbuf_t *m = s_mbuf_freelist;
    s_mbuf_freelist = m->m_next;
    spinlock_unlock_irqrestore(&s_mbuf_lock, flags);

    __builtin_memset(m, 0, sizeof(mbuf_t));
    m->m_type = type;
    m->m_data = m->m_dat;
    m->m_len = 0;
    return m;
}

mbuf_t *m_gethdr(u16 type) {
    mbuf_t *m = m_get(type);
    if (m) {
        m->m_flags |= M_PKTHDR;
        m->m_data = m->m_pktdat;
    }
    return m;
}

mbuf_t *m_getcl(u16 type) {
    mbuf_t *m = m_gethdr(type);
    if (!m) return nullptr;

    // associate cluster
    usize idx = (usize)(m - s_mbuf_pool);
    m->m_flags |= M_EXT;
    m->m_ext.ext_buf = s_cluster_pool[idx];
    m->m_ext.ext_size = MCLBYTES;
    m->m_ext.ext_ref = 1;
    m->m_data = m->m_ext.ext_buf;
    return m;
}

void m_free(mbuf_t *m) {
    if (!m) return;

    irq_flags_t flags = spinlock_lock_irqsave(&s_mbuf_lock);
    m->m_type = MT_FREE;
    m->m_flags = 0;
    m->m_len = 0;
    m->m_nextpkt = nullptr;
    m->m_next = s_mbuf_freelist;
    s_mbuf_freelist = m;
    spinlock_unlock_irqrestore(&s_mbuf_lock, flags);
}

void m_freem(mbuf_t *m) {
    while (m) {
        mbuf_t *next = m->m_next;
        m_free(m);
        m = next;
    }
}

void m_copydata(const mbuf_t *m, i32 off, i32 len, void *cp) {
    u8 *dst = (u8 *)cp;
    while (off > 0) {
        if (!m) return;
        if (off < m->m_len) break;
        off -= m->m_len;
        m = m->m_next;
    }

    while (len > 0 && m) {
        i32 count = m->m_len - off;
        if (count > len) count = len;
        __builtin_memcpy(dst, m->m_data + off, (usize)count);
        dst += count;
        len -= count;
        off = 0;
        m = m->m_next;
    }
}

chimera_error_t m_append(mbuf_t *m, i32 len, const void *cp) {
    if (!m || len <= 0 || !cp) return CHIMERA_ERR_INVALID;

    // navigate to last mbuf
    while (m->m_next) m = m->m_next;

    usize max_capacity = (m->m_flags & M_EXT) ? m->m_ext.ext_size :
                         (m->m_flags & M_PKTHDR) ? sizeof(m->m_pktdat) : sizeof(m->m_dat);
    u8 *buf_start = (m->m_flags & M_EXT) ? m->m_ext.ext_buf :
                    (m->m_flags & M_PKTHDR) ? m->m_pktdat : m->m_dat;

    usize current_offset = (usize)(m->m_data - buf_start);
    if (current_offset + (usize)m->m_len + (usize)len <= max_capacity) {
        __builtin_memcpy(m->m_data + m->m_len, cp, (usize)len);
        m->m_len += len;
        return CHIMERA_SUCCESS;
    }

    // allocate next chained mbuf
    mbuf_t *n = (len > (i32)sizeof(m->m_dat)) ? m_getcl(m->m_type) : m_get(m->m_type);
    if (!n) return CHIMERA_ERR_NOMEM;

    __builtin_memcpy(n->m_data, cp, (usize)len);
    n->m_len = len;
    m->m_next = n;
    return CHIMERA_SUCCESS;
}
