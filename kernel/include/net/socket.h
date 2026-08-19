// bsd socket subsystem
#pragma once
#ifndef XIU_NET_SOCKET_H
#define XIU_NET_SOCKET_H

#include <kernel/xiu_types.h>
#include <kernel/spinlock.h>
#include <net/mbuf.h>

#define AF_UNSPEC       0
#define AF_UNIX         1
#define AF_LOCAL        AF_UNIX
#define AF_INET         2
#define AF_INET6        30
#define AF_MAX          32

#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define SOCK_RAW        3

#define IPPROTO_IP      0
#define IPPROTO_ICMP    1
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17
#define IPPROTO_RAW     255

#define SOL_SOCKET      0xffff

#define SO_DEBUG        0x0001
#define SO_ACCEPTCONN   0x0002
#define SO_REUSEADDR    0x0004
#define SO_KEEPALIVE    0x0008
#define SO_DONTROUTE    0x0010
#define SO_BROADCAST    0x0020
#define SO_RCVBUF       0x1002
#define SO_SNDBUF       0x1001
#define SO_RCVTIMEO     0x1006
#define SO_SNDTIMEO     0x1005
#define SO_ERROR        0x1007
#define SO_TYPE         0x1008

#define SHUT_RD         0
#define SHUT_WR         1
#define SHUT_RDWR       2

#define SS_NOFDREF          0x0001
#define SS_ISCONNECTED      0x0002
#define SS_ISCONNECTING     0x0004
#define SS_ISDISCONNECTING  0x0008
#define SS_CANTSENDMORE     0x0010
#define SS_CANTRCVMORE      0x0020
#define SS_RCVATMARK        0x0040
#define SS_NBIO             0x0100

#define INADDR_ANY          ((u32)0x00000000)
#define INADDR_LOOPBACK     ((u32)0x7F000001)
#define INADDR_BROADCAST    ((u32)0xFFFFFFFF)

typedef u8 sa_family_t;
typedef u16 in_port_t;
typedef u32 in_addr_t;

struct in_addr {
    in_addr_t           s_addr;
};

struct sockaddr {
    u8                  sa_len;
    sa_family_t         sa_family;
    char                sa_data[14];
};

struct sockaddr_in {
    u8                  sin_len;
    sa_family_t         sin_family;
    in_port_t           sin_port;
    struct in_addr      sin_addr;
    char                sin_zero[8];
};

#define SB_MAX          (256 * 1024)

struct sockbuf {
    u32                 sb_cc;
    u32                 sb_hiwat;
    mbuf_t             *sb_mb;
    mbuf_t             *sb_last;
    spinlock_t          sb_lock;
};

struct socket;
struct protosw;

struct pr_usrreqs {
    xiu_error_t (*pru_attach)(struct socket *so, int proto);
    xiu_error_t (*pru_bind)(struct socket *so, struct sockaddr *nam);
    xiu_error_t (*pru_connect)(struct socket *so, struct sockaddr *nam);
    xiu_error_t (*pru_listen)(struct socket *so, int backlog);
    xiu_error_t (*pru_accept)(struct socket *so, struct sockaddr **nam);
    xiu_error_t (*pru_send)(struct socket *so, int flags, mbuf_t *m, struct sockaddr *nam, mbuf_t *control);
    xiu_error_t (*pru_receive)(struct socket *so, struct sockaddr **nam, mbuf_t **mp, int *flags);
    xiu_error_t (*pru_disconnect)(struct socket *so);
    xiu_error_t (*pru_shutdown)(struct socket *so);
    xiu_error_t (*pru_detach)(struct socket *so);
};

typedef struct socket {
    u64                 so_signature;
    i16                 so_type;
    u16                 so_state;
    void               *so_pcb;
    struct protosw     *so_proto;

    struct socket      *so_head;
    struct socket      *so_q0;
    struct socket      *so_q;
    i16                 so_qlimit;
    i16                 so_qlen;

    struct sockbuf      so_snd;
    struct sockbuf      so_rcv;

    i32                 so_error;
    _Atomic(u32)        so_refcount;
    spinlock_t          so_lock;
} socket_t;

#define XIU_SOCKET_MAGIC 0x534F434B45542121ULL

#ifdef __cplusplus
extern "C" {
#endif

xiu_error_t socreate(int dom, socket_t **aso, int type, int proto);
xiu_error_t sobind(socket_t *so, struct sockaddr *nam);
xiu_error_t solisten(socket_t *so, int backlog);
xiu_error_t soconnect(socket_t *so, struct sockaddr *nam);
xiu_error_t soaccept(socket_t *so, struct sockaddr **nam, socket_t **new_so);
xiu_error_t sosend(socket_t *so, struct sockaddr *addr, const void *buf, usize len, int flags);
xiu_error_t soreceive(socket_t *so, struct sockaddr **addr, void *buf, usize len, usize *out_len, int flags);
xiu_error_t soshutdown(socket_t *so, int how);
xiu_error_t soclose(socket_t *so);

void        sbappend(struct sockbuf *sb, mbuf_t *m);
void        sbflush(struct sockbuf *sb);

#ifdef __cplusplus
}
#endif

#endif
