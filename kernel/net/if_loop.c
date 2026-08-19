/* =============================================================================
 * XIU Operating System — Loopback Network Interface (lo0)
 * kernel/net/if_loop.c
 * ============================================================================= */

#include <net/if.h>
#include <net/mbuf.h>
#include <net/protocols.h>

static ifnet_t s_loopback_if;

static xiu_error_t loopback_output(ifnet_t *ifp, mbuf_t *m, struct in_addr dest_ip) {
    (void)dest_ip;
    if (!m) return XIU_ERR_INVALID;

    ifp->if_data.ifi_opackets++;
    ifp->if_data.ifi_obytes += m->m_pkthdr.len;

    // loop back directly to input
    ifp->if_data.ifi_ipackets++;
    ifp->if_data.ifi_ibytes += m->m_pkthdr.len;

    if_input(ifp, m);
    return XIU_SUCCESS;
}

void loopback_init(void) {
    __builtin_memset(&s_loopback_if, 0, sizeof(s_loopback_if));
    __builtin_strncpy(s_loopback_if.if_name, "lo0", IFNAMSIZ - 1);
    s_loopback_if.if_unit = 0;
    s_loopback_if.if_flags = IFF_UP | IFF_LOOPBACK | IFF_RUNNING;
    s_loopback_if.if_mtu = 16384;

    s_loopback_if.if_ip.s_addr = htonl(INADDR_LOOPBACK);       // 127.0.0.1
    s_loopback_if.if_netmask.s_addr = htonl(0xFF000000);      // 255.0.0.0
    s_loopback_if.if_bcast.s_addr = htonl(0x7FFFFFFF);

    s_loopback_if.if_output = loopback_output;
    if_attach(&s_loopback_if);
}
