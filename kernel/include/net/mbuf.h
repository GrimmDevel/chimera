// memory buffer (mbuf) subsystem
#pragma once
#ifndef CHIMERA_NET_MBUF_H
#define CHIMERA_NET_MBUF_H

#include <kernel/chimera_types.h>
#include <kernel/spinlock.h>

#define MLEN            256
#define MCLBYTES        2048

#define MT_FREE         0
#define MT_DATA         1
#define MT_HEADER       2
#define MT_SONAME       3
#define MT_CONTROL      4

#define M_EXT           0x0001
#define M_PKTHDR        0x0002
#define M_EOR           0x0004
#define M_BCAST         0x0008
#define M_MCAST         0x0010

struct pkthdr {
    struct ifnet       *rcvif;
    i32                 len;
    u16                 csum_flags;
    u16                 csum_data;
};

struct m_ext {
    u8                 *ext_buf;
    void              (*ext_free)(u8 *, usize);
    usize               ext_size;
    _Atomic(u32)        ext_ref;
};

typedef struct mbuf {
    struct mbuf        *m_next;
    struct mbuf        *m_nextpkt;
    u8                 *m_data;
    i32                 m_len;
    u16                 m_type;
    u16                 m_flags;

    union {
        struct {
            struct pkthdr   m_pkthdr;
            union {
                struct m_ext    m_ext;
                u8              m_pktdat[MLEN - sizeof(struct pkthdr)];
            } m_pku;
        } m_pkt;
        u8                  m_dat[MLEN];
    } m_u;
} mbuf_t;

#define m_pkthdr    m_u.m_pkt.m_pkthdr
#define m_ext       m_u.m_pkt.m_pku.m_ext
#define m_pktdat    m_u.m_pkt.m_pku.m_pktdat
#define m_dat       m_u.m_dat

#ifdef __cplusplus
extern "C" {
#endif

void    mbuf_init(void);
mbuf_t *m_get(u16 type);
mbuf_t *m_gethdr(u16 type);
mbuf_t *m_getcl(u16 type);
void    m_free(mbuf_t *m);
void    m_freem(mbuf_t *m);
mbuf_t *m_copym(mbuf_t *m, i32 off, i32 len);
void    m_copydata(const mbuf_t *m, i32 off, i32 len, void *cp);
chimera_error_t m_append(mbuf_t *m, i32 len, const void *cp);
mbuf_t *m_pullup(mbuf_t *m, i32 len);

#ifdef __cplusplus
}
#endif

#endif
