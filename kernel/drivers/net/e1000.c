// intel 8254x/82574l (e1000) pcie nic driver
#include <net/if.h>
#include <net/mbuf.h>
#include <net/protocols.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>

extern xiu_paddr_t pmm_alloc_pages(usize count);
extern u64 g_hhdm_base;

#define E1000_NUM_RX_DESC 32
#define E1000_NUM_TX_DESC 32
#define E1000_RX_BUF_SIZE 2048

#define REG_CTRL        0x0000
#define REG_STATUS      0x0008
#define REG_EERD        0x0014
#define REG_ICR         0x00C0
#define REG_IMS         0x00D0
#define REG_IMC         0x00D8
#define REG_RCTL        0x0100
#define REG_TCTL        0x0400
#define REG_TIPG        0x0410
#define REG_RDBAL       0x2800
#define REG_RDBAH       0x2804
#define REG_RDLEN       0x2808
#define REG_RDH         0x2810
#define REG_RDT         0x2818
#define REG_TDBAL       0x3800
#define REG_TDBAH       0x3804
#define REG_TDLEN       0x3808
#define REG_TDH         0x3810
#define REG_TDT         0x3818
#define REG_MTA         0x5200
#define REG_RAL         0x5400
#define REG_RAH         0x5404

#define ECTRL_SLU       (1u << 6)
#define ECTRL_RST       (1u << 26)

#define RCTL_EN         (1u << 1)
#define RCTL_SBP        (1u << 2)
#define RCTL_UPE        (1u << 3)
#define RCTL_MPE        (1u << 4)
#define RCTL_BAM        (1u << 15)
#define RCTL_SZ_2048    (0u << 16)
#define RCTL_SECRC      (1u << 26)

#define TCTL_EN         (1u << 1)
#define TCTL_PSP        (1u << 3)
#define TCTL_CT_SHIFT   4
#define TCTL_COLD_SHIFT 12

#define RDESC_STAT_DD   (1u << 0)
#define RDESC_STAT_EOP  (1u << 1)

#define TDESC_CMD_EOP   (1u << 0)
#define TDESC_CMD_IFCS  (1u << 1)
#define TDESC_CMD_RS    (1u << 3)
#define TDESC_STAT_DD   (1u << 0)

typedef struct XIU_PACKED e1000_rx_desc {
    u64                 addr;
    u16                 length;
    u16                 checksum;
    u8                  status;
    u8                  errors;
    u16                 special;
} e1000_rx_desc_t;

typedef struct XIU_PACKED e1000_tx_desc {
    u64                 addr;
    u16                 length;
    u8                  cso;
    u8                  cmd;
    u8                  status;
    u8                  css;
    u16                 special;
} e1000_tx_desc_t;

typedef struct e1000_softc {
    u64                 mmio_base;
    u64                 mmio_phys;

    xiu_paddr_t         rx_desc_phys;
    e1000_rx_desc_t    *rx_descs;
    xiu_paddr_t         rx_buf_phys;
    u8                 *rx_buffers[E1000_NUM_RX_DESC];

    xiu_paddr_t         tx_desc_phys;
    e1000_tx_desc_t    *tx_descs;
    xiu_paddr_t         tx_buf_phys;
    u8                 *tx_buffers[E1000_NUM_TX_DESC];

    u16                 rx_cur;
    u16                 tx_cur;
    u8                  mac[ETHER_ADDR_LEN];

    ifnet_t             ifnet;
    spinlock_t          lock;
} e1000_softc_t;

static e1000_softc_t g_e1000;
static bool          g_e1000_present = false;

static inline u32 e1000_read(e1000_softc_t *sc, u32 reg) {
    return *(volatile u32 *)(sc->mmio_base + reg);
}

static inline void e1000_write(e1000_softc_t *sc, u32 reg, u32 val) {
    *(volatile u32 *)(sc->mmio_base + reg) = val;
}

static u16 e1000_read_eeprom(e1000_softc_t *sc, u8 addr) {
    e1000_write(sc, REG_EERD, ((u32)addr << 8) | 1);
    u32 val = 0;
    int timeout = 10000;
    while (timeout-- > 0 && !((val = e1000_read(sc, REG_EERD)) & (1 << 4))) {
        cpu_relax();
    }
    return (u16)((val >> 16) & 0xFFFF);
}

static void e1000_read_mac(e1000_softc_t *sc) {
    u32 ral = e1000_read(sc, REG_RAL);
    u32 rah = e1000_read(sc, REG_RAH);

    sc->mac[0] = (u8)(ral & 0xFF);
    sc->mac[1] = (u8)((ral >> 8) & 0xFF);
    sc->mac[2] = (u8)((ral >> 16) & 0xFF);
    sc->mac[3] = (u8)((ral >> 24) & 0xFF);
    sc->mac[4] = (u8)(rah & 0xFF);
    sc->mac[5] = (u8)((rah >> 8) & 0xFF);

    if ((sc->mac[0] == 0 && sc->mac[1] == 0 && sc->mac[2] == 0) ||
        (sc->mac[0] == 0xFF && sc->mac[1] == 0xFF)) {
        u16 w0 = e1000_read_eeprom(sc, 0);
        u16 w1 = e1000_read_eeprom(sc, 1);
        u16 w2 = e1000_read_eeprom(sc, 2);
        sc->mac[0] = (u8)(w0 & 0xFF);
        sc->mac[1] = (u8)(w0 >> 8);
        sc->mac[2] = (u8)(w1 & 0xFF);
        sc->mac[3] = (u8)(w1 >> 8);
        sc->mac[4] = (u8)(w2 & 0xFF);
        sc->mac[5] = (u8)(w2 >> 8);
    }

    if ((sc->mac[0] == 0 && sc->mac[1] == 0 && sc->mac[2] == 0) ||
        (sc->mac[0] == 0xFF && sc->mac[1] == 0xFF)) {
        sc->mac[0] = 0x52; sc->mac[1] = 0x54; sc->mac[2] = 0x00;
        sc->mac[3] = 0x12; sc->mac[4] = 0x34; sc->mac[5] = 0x56;
    }
}

static xiu_error_t e1000_transmit_frame(const void *data, usize len) {
    if (!g_e1000_present || len == 0 || len > E1000_RX_BUF_SIZE) return XIU_ERR_INVALID;

    e1000_softc_t *sc = &g_e1000;
    irq_flags_t flags = spinlock_lock_irqsave(&sc->lock);

    u16 cur = sc->tx_cur;
    volatile e1000_tx_desc_t *desc = &sc->tx_descs[cur];

    __builtin_memcpy(sc->tx_buffers[cur], data, len);
    if (len < 60) {
        __builtin_memset(sc->tx_buffers[cur] + len, 0, 60 - len);
        len = 60;
    }
    desc->cso = 0;
    desc->css = 0;
    desc->special = 0;
    desc->length = (u16)len;
    desc->cmd = TDESC_CMD_EOP | TDESC_CMD_IFCS | TDESC_CMD_RS;
    desc->status = 0;

    sc->tx_cur = (cur + 1) % E1000_NUM_TX_DESC;
    e1000_write(sc, REG_TDT, sc->tx_cur);

    int timeout = 50000;
    while (!(*(volatile u8 *)&desc->status & TDESC_STAT_DD) && timeout-- > 0) {
        cpu_relax();
    }

    sc->ifnet.if_data.ifi_opackets++;
    sc->ifnet.if_data.ifi_obytes += len;

    spinlock_unlock_irqrestore(&sc->lock, flags);
    return XIU_SUCCESS;
}

static xiu_error_t e1000_ifnet_output(ifnet_t *ifp, mbuf_t *m, struct in_addr dest_ip);

void e1000_poll_rx(void) {
    if (!g_e1000_present) return;

    e1000_softc_t *sc = &g_e1000;
    irq_flags_t flags = spinlock_lock_irqsave(&sc->lock);

    while (*(volatile u8 *)&sc->rx_descs[sc->rx_cur].status & RDESC_STAT_DD) {
        u16 cur = sc->rx_cur;
        volatile e1000_rx_desc_t *desc = &sc->rx_descs[cur];
        u16 len = desc->length;

        if (len > 0) {
            mbuf_t *m = m_getcl(MT_DATA);
            if (m) {
                __builtin_memcpy(m->m_data, sc->rx_buffers[cur], len);
                m->m_len = len;
                m->m_pkthdr.len = len;
                m->m_pkthdr.rcvif = &sc->ifnet;

                sc->ifnet.if_data.ifi_ipackets++;
                sc->ifnet.if_data.ifi_ibytes += len;

                spinlock_unlock_irqrestore(&sc->lock, flags);
                if_input(&sc->ifnet, m);
                flags = spinlock_lock_irqsave(&sc->lock);
            }
        }

        desc->status = 0;
        u16 old_cur = sc->rx_cur;
        sc->rx_cur = (cur + 1) % E1000_NUM_RX_DESC;
        e1000_write(sc, REG_RDT, old_cur);
    }

    spinlock_unlock_irqrestore(&sc->lock, flags);
}

static xiu_error_t e1000_ifnet_output(ifnet_t *ifp, mbuf_t *m, struct in_addr dest_ip) {
    (void)ifp;
    (void)dest_ip;
    if (!m) return XIU_ERR_INVALID;

    u8 packet_buf[1536];
    usize total_len = 0;
    mbuf_t *curr = m;
    while (curr && total_len + (usize)curr->m_len <= sizeof(packet_buf)) {
        __builtin_memcpy(packet_buf + total_len, curr->m_data, (usize)curr->m_len);
        total_len += (usize)curr->m_len;
        curr = curr->m_next;
    }
    m_freem(m);

    return e1000_transmit_frame(packet_buf, total_len);
}

xiu_error_t e1000_init(u64 bar0_phys) {
    e1000_softc_t *sc = &g_e1000;
    __builtin_memset(sc, 0, sizeof(*sc));

    sc->mmio_phys = bar0_phys;
    sc->mmio_base = g_hhdm_base + bar0_phys;
    spinlock_init(&sc->lock);

    // allocate physical dma pages
    sc->rx_desc_phys = pmm_alloc_pages(1);
    if (!sc->rx_desc_phys) return XIU_ERR_NOMEM;
    sc->rx_descs = (e1000_rx_desc_t *)(sc->rx_desc_phys + g_hhdm_base);
    __builtin_memset(sc->rx_descs, 0, 4096);

    sc->tx_desc_phys = pmm_alloc_pages(1);
    if (!sc->tx_desc_phys) return XIU_ERR_NOMEM;
    sc->tx_descs = (e1000_tx_desc_t *)(sc->tx_desc_phys + g_hhdm_base);
    __builtin_memset(sc->tx_descs, 0, 4096);

    sc->rx_buf_phys = pmm_alloc_pages(16);
    if (!sc->rx_buf_phys) return XIU_ERR_NOMEM;
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        sc->rx_buffers[i] = (u8 *)(sc->rx_buf_phys + g_hhdm_base + (i * E1000_RX_BUF_SIZE));
        sc->rx_descs[i].addr = sc->rx_buf_phys + (i * E1000_RX_BUF_SIZE);
        sc->rx_descs[i].status = 0;
    }

    sc->tx_buf_phys = pmm_alloc_pages(16);
    if (!sc->tx_buf_phys) return XIU_ERR_NOMEM;
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        sc->tx_buffers[i] = (u8 *)(sc->tx_buf_phys + g_hhdm_base + (i * E1000_RX_BUF_SIZE));
        sc->tx_descs[i].addr = sc->tx_buf_phys + (i * E1000_RX_BUF_SIZE);
        sc->tx_descs[i].status = TDESC_STAT_DD;
        sc->tx_descs[i].cmd = 0;
    }

    // reset device
    e1000_write(sc, REG_CTRL, e1000_read(sc, REG_CTRL) | ECTRL_RST);
    for (volatile int i = 0; i < 500000; i++) cpu_relax();

    // mask all interrupts
    e1000_write(sc, REG_IMC, 0xFFFFFFFF);
    e1000_read(sc, REG_ICR);

    // read mac
    e1000_read_mac(sc);
    kprintf("[e1000] MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
            sc->mac[0], sc->mac[1], sc->mac[2], sc->mac[3], sc->mac[4], sc->mac[5]);

    // link up
    e1000_write(sc, REG_CTRL, e1000_read(sc, REG_CTRL) | ECTRL_SLU);

    for (int i = 0; i < 128; i++) e1000_write(sc, REG_MTA + (i * 4), 0);

    // rx setup
    e1000_write(sc, REG_RDBAL, (u32)(sc->rx_desc_phys & 0xFFFFFFFF));
    e1000_write(sc, REG_RDBAH, (u32)(sc->rx_desc_phys >> 32));
    e1000_write(sc, REG_RDLEN, E1000_NUM_RX_DESC * sizeof(e1000_rx_desc_t));
    e1000_write(sc, REG_RDH, 0);
    e1000_write(sc, REG_RDT, E1000_NUM_RX_DESC - 1);
    sc->rx_cur = 0;

    e1000_write(sc, REG_RCTL, RCTL_EN | RCTL_SBP | RCTL_UPE | RCTL_MPE | RCTL_BAM | RCTL_SZ_2048 | RCTL_SECRC);

    // tx setup
    e1000_write(sc, REG_TDBAL, (u32)(sc->tx_desc_phys & 0xFFFFFFFF));
    e1000_write(sc, REG_TDBAH, (u32)(sc->tx_desc_phys >> 32));
    e1000_write(sc, REG_TDLEN, E1000_NUM_TX_DESC * sizeof(e1000_tx_desc_t));
    e1000_write(sc, REG_TDH, 0);
    e1000_write(sc, REG_TDT, 0);
    sc->tx_cur = 0;

    e1000_write(sc, REG_TIPG, 0x0060200A);
    e1000_write(sc, REG_TCTL, TCTL_EN | TCTL_PSP | (15 << TCTL_CT_SHIFT) | (64 << TCTL_COLD_SHIFT));

    // attach en0
    __builtin_strncpy(sc->ifnet.if_name, "en0", IFNAMSIZ - 1);
    sc->ifnet.if_unit = 0;
    sc->ifnet.if_flags = IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_MULTICAST;
    sc->ifnet.if_mtu = 1500;
    __builtin_memcpy(sc->ifnet.if_mac, sc->mac, ETHER_ADDR_LEN);
    sc->ifnet.if_output = e1000_ifnet_output;
    sc->ifnet.if_softc = sc;

    if_attach(&sc->ifnet);
    g_e1000_present = true;

    kprintf("  [  OK  ]  Intel e1000 Gigabit Ethernet (en0 attached)\n");
    return XIU_SUCCESS;
}
