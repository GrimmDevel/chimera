// network interface (ifnet) architecture
#pragma once
#ifndef XIU_NET_IF_H
#define XIU_NET_IF_H

#include <kernel/xiu_types.h>
#include <kernel/spinlock.h>
#include <net/mbuf.h>
#include <net/socket.h>

#define IFNAMSIZ        16
#define ETHER_ADDR_LEN  6

#define IFF_UP          0x0001
#define IFF_BROADCAST   0x0002
#define IFF_DEBUG       0x0004
#define IFF_LOOPBACK    0x0008
#define IFF_RUNNING     0x0040
#define IFF_MULTICAST   0x8000

struct if_data {
    u64                 ifi_ipackets;
    u64                 ifi_opackets;
    u64                 ifi_ibytes;
    u64                 ifi_obytes;
    u64                 ifi_ierrors;
    u64                 ifi_oerrors;
};

typedef struct ifnet {
    char                if_name[IFNAMSIZ];
    u16                 if_unit;
    u16                 if_flags;
    u32                 if_mtu;
    
    u8                  if_mac[ETHER_ADDR_LEN];
    
    struct in_addr      if_ip;
    struct in_addr      if_netmask;
    struct in_addr      if_bcast;
    struct in_addr      if_gateway;
    struct in_addr      if_dns;

    struct if_data      if_data;

    xiu_error_t       (*if_output)(struct ifnet *ifp, mbuf_t *m, struct in_addr dest_ip);
    xiu_error_t       (*if_ioctl)(struct ifnet *ifp, u64 cmd, void *data);

    void               *if_softc;
    struct ifnet       *if_next;
    spinlock_t          if_lock;
} ifnet_t;

#ifdef __cplusplus
extern "C" {
#endif

void        if_init(void);
xiu_error_t if_attach(ifnet_t *ifp);
void        if_detach(ifnet_t *ifp);
ifnet_t    *if_lookup(const char *name);
ifnet_t    *if_get_default(void);
ifnet_t    *if_get_list(void);
void        if_input(ifnet_t *ifp, mbuf_t *m);

#ifdef __cplusplus
}
#endif

#endif
