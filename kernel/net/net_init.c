/* =============================================================================
 * XIU Operating System — Network Subsystem Initializer (net_init)
 * kernel/net/net_init.c
 * ============================================================================= */

#include <net/if.h>
#include <net/mbuf.h>
#include <net/socket.h>
#include <net/protocols.h>
#include <kernel/panic.h>

extern void mbuf_init(void);
extern void if_init(void);
extern void loopback_init(void);
extern void arp_init(void);
extern void icmp_init(void);
extern void udp_init(void);
extern void tcp_init(void);
extern void uipc_socket_init(void);
extern xiu_error_t e1000_init(u64 bar0_phys);
extern void dhcp_discover(ifnet_t *ifp);

void net_init(void) {
    kprintf("[net] Initializing XIU-Net Darwin BSD Network Subsystem...\n");

    mbuf_init();
    if_init();
    loopback_init();
    arp_init();
    icmp_init();
    udp_init();
    tcp_init();
    uipc_socket_init();

    // check if Intel e1000 PCI card was found by XIUKit PCI probe
    extern u64 g_e1000_pci_bar0;
    if (g_e1000_pci_bar0 != 0) {
        kprintf("[net] Found Intel Gigabit NIC at BAR0: 0x%llx\n", (unsigned long long)g_e1000_pci_bar0);
        e1000_init(g_e1000_pci_bar0);
        ifnet_t *en0 = if_lookup("en0");
        if (en0) {
            dhcp_discover(en0);
        }
    } else {
        kprintf("[net] No supported Ethernet NIC found on PCI bus.\n");
    }

    kprintf("  [  OK  ]  XIU-Net Darwin BSD Network Subsystem\n");
}
