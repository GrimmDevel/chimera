/* =============================================================================
 * Chimera Operating System — AHCI / Serial ATA Storage Driver
 * kernel/chimerakit/ahci.cpp
 *
 * Implements full AHCI 1.3 specification for SATA Host Bus Adapters (HBA),
 * port enumeration, Command List, Frame Information Structure (FIS), and
 * Physical Region Descriptor Table (PRDT) DMA block transfers.
 * ============================================================================= */

#include <kernel/chimera_types.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>

#ifndef CHIMERA_ERR_IO
#define CHIMERA_ERR_IO CHIMERA_ERR_GENERIC
#endif

extern "C" {
    void kprintf(const char *fmt, ...);
    chimera_paddr_t pmm_alloc_page(void);
    void pmm_free_page(chimera_paddr_t addr);
    u64 pmap_kernel_pml4(void);
    u64 pmap_vtophys(u64 pml4_phys, u64 vaddr);
}

namespace XIUKit {

#define HBA_PORT_IPM_ACTIVE  1
#define HBA_PORT_DET_PRESENT 3

#define HBA_CMD_ST   (1u << 0)
#define HBA_CMD_FRE  (1u << 4)
#define HBA_CMD_FR   (1u << 14)
#define HBA_CMD_CR   (1u << 15)

#define ATA_DEV_BUSY (1u << 7)
#define ATA_DEV_DRQ  (1u << 3)
#define ATA_DEV_ERR  (1u << 0)

#define SATA_SIG_ATA   0x00000101u
#define SATA_SIG_ATAPI 0xEB140101u
#define SATA_SIG_SEMB  0xC33C0101u
#define SATA_SIG_PM    0x96690101u

#define ATA_CMD_READ_DMA_EXT  0x25u
#define ATA_CMD_WRITE_DMA_EXT 0x35u
#define ATA_CMD_IDENTIFY      0xECu

#define FIS_TYPE_REG_H2D 0x27u

// fis Register — Host to Device
typedef struct CHIMERA_PACKED {
    u8  fis_type;
    u8  pmport:4;
    u8  rsv0:3;
    u8  c:1;
    u8  command;
    u8  featurel;
    u8  lba0;
    u8  lba1;
    u8  lba2;
    u8  device;
    u8  lba3;
    u8  lba4;
    u8  lba5;
    u8  featureh;
    u8  countl;
    u8  counth;
    u8  icc;
    u8  control;
    u8  rsv1[4];
} fis_reg_h2d_t;

// physical Region Descriptor Table (PRDT) entry
typedef struct CHIMERA_PACKED {
    u32 dba;
    u32 dbau;
    u32 rsv0;
    u32 dbc:22;
    u32 rsv1:9;
    u32 i:1;
} hba_prdt_entry_t;

// command Table
typedef struct CHIMERA_PACKED {
    u8               cfis[64];
    u8               acmd[16];
    u8               rsv[48];
    hba_prdt_entry_t prdt_entry[1];
} hba_cmd_tbl_t;

// command Header
typedef struct CHIMERA_PACKED {
    u8           cfl:5;
    u8           a:1;
    u8           w:1;
    u8           p:1;
    u8           r:1;
    u8           b:1;
    u8           c:1;
    u8           rsv0:1;
    u8           pmp:4;
    u16          prdtl;
    volatile u32 prdbc;
    u32          ctba;
    u32          ctbau;
    u32          rsv1[4];
} hba_cmd_header_t;

// hba Port Registers
typedef volatile struct CHIMERA_PACKED {
    u32 clb;
    u32 clbu;
    u32 fb;
    u32 fbu;
    u32 is;
    u32 ie;
    u32 cmd;
    u32 rsv0;
    u32 tfd;
    u32 sig;
    u32 ssts;
    u32 sctl;
    u32 serr;
    u32 sact;
    u32 ci;
    u32 sntf;
    u32 fbs;
    u32 rsv1[11];
    u32 vendor[4];
} hba_port_t;

// generic Host Control / Memory Registers
typedef volatile struct CHIMERA_PACKED {
    u32        cap;
    u32        ghc;
    u32        is;
    u32        pi;
    u32        vs;
    u32        ccc_ctl;
    u32        ccc_pts;
    u32        em_loc;
    u32        em_ctl;
    u32        cap2;
    u32        bohc;
    u8         rsv[0xA0 - 0x2C];
    u8         vendor[0x100 - 0xA0];
    hba_port_t ports[32];
} hba_mem_t;

class AHCIDriver {
public:
    AHCIDriver(u64 abar_phys) : m_abar_phys(abar_phys), m_hba(nullptr), m_port_count(0) {
        spinlock_init(&m_lock);
    }

    void init() {
        if (!m_abar_phys) {
            kprintf("[ChimeraKit] AHCI: Invalid ABAR physical address\n");
            return;
        }

        m_hba = (hba_mem_t *)(m_abar_phys + g_hhdm_base);
        kprintf("[ChimeraKit] AHCI: Initializing controller (ABAR=%p, Version %x.%x)\n",
                (void *)m_abar_phys, (m_hba->vs >> 16), (m_hba->vs & 0xFFFF));

        // enable AHCI mode in Global Host Control
        m_hba->ghc |= (1u << 31); // GHC.AE (AHCI Enable)

        u32 pi = m_hba->pi;
        m_port_count = 0;

        for (u32 i = 0; i < 32; i++) {
            if (pi & (1u << i)) {
                hba_port_t *port = &m_hba->ports[i];
                u32 ssts = port->ssts;
                u8 ipm = (u8)((ssts >> 8) & 0x0F);
                u8 det = (u8)(ssts & 0x0F);

                if (det == HBA_PORT_DET_PRESENT && ipm == HBA_PORT_IPM_ACTIVE) {
                    u32 sig = port->sig;
                    if (sig == SATA_SIG_ATA) {
                        kprintf("        ahci: port %u -> Connected SATA Drive (sig=0x%08x)\n", i, sig);
                        rebase_port(port, i);
                        m_active_ports[m_port_count++] = i;
                    } else if (sig == SATA_SIG_ATAPI) {
                        kprintf("        ahci: port %u -> Connected SATAPI Optical Drive (sig=0x%08x)\n", i, sig);
                    }
                }
            }
        }

        if (m_port_count == 0) {
            kprintf("        ahci: no active SATA drives found on HBA ports\n");
        }
    }

    chimera_error_t read_blocks(u32 port_idx, u64 lba, u32 count, void *buffer) {
        if (!m_hba || count == 0 || !buffer) return CHIMERA_ERR_INVALID;

        hba_port_t *port = nullptr;
        bool found = false;
        for (u32 i = 0; i < m_port_count; i++) {
            if (m_active_ports[i] == port_idx) {
                port = &m_hba->ports[port_idx];
                found = true;
                break;
            }
        }
        if (!found || !port) return CHIMERA_ERR_NOTFOUND;

        irq_flags_t flags = spinlock_lock_irqsave(&m_lock);

        // clear interrupt and error status
        port->is   = (u32)-1;
        port->serr = (u32)-1;

        u64 clb_phys = ((u64)port->clbu << 32) | port->clb;
        hba_cmd_header_t *cmdheader = (hba_cmd_header_t *)(clb_phys + g_hhdm_base);

        cmdheader->cfl   = sizeof(fis_reg_h2d_t) / sizeof(u32);
        cmdheader->w     = 0; // read
        cmdheader->prdtl = 1;

        u64 ctba_phys = ((u64)cmdheader->ctbau << 32) | cmdheader->ctba;
        hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t *)(ctba_phys + g_hhdm_base);
        __builtin_memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t));

        // buffer physical address
        u64 buf_vaddr = (u64)buffer;
        u64 buf_phys = pmap_vtophys(pmap_kernel_pml4(), buf_vaddr);
        if (!buf_phys) {
            spinlock_unlock_irqrestore(&m_lock, flags);
            return CHIMERA_ERR_INVALID;
        }

        cmdtbl->prdt_entry[0].dba  = (u32)(buf_phys & 0xFFFFFFFF);
        cmdtbl->prdt_entry[0].dbau = (u32)((buf_phys >> 32) & 0xFFFFFFFF);
        cmdtbl->prdt_entry[0].dbc  = (count * 512) - 1;
        cmdtbl->prdt_entry[0].i    = 1;

        // construct FIS Host to Device
        fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t *)(&cmdtbl->cfis[0]);
        cmdfis->fis_type = FIS_TYPE_REG_H2D;
        cmdfis->c        = 1; // command
        cmdfis->command  = ATA_CMD_READ_DMA_EXT;

        cmdfis->lba0 = (u8)lba;
        cmdfis->lba1 = (u8)(lba >> 8);
        cmdfis->lba2 = (u8)(lba >> 16);
        cmdfis->device = 1u << 6; // LBA mode

        cmdfis->lba3 = (u8)(lba >> 24);
        cmdfis->lba4 = (u8)(lba >> 32);
        cmdfis->lba5 = (u8)(lba >> 40);

        cmdfis->countl = (u8)(count & 0xFF);
        cmdfis->counth = (u8)((count >> 8) & 0xFF);

        // issue command slot 0
        port->ci = 1u;

        // poll for completion
        u32 timeout = 1000000;
        while (timeout--) {
            if (!(port->ci & 1u)) break;
            if (port->is & (1u << 30)) { // task file error status
                spinlock_unlock_irqrestore(&m_lock, flags);
                return CHIMERA_ERR_IO;
            }
        }

        spinlock_unlock_irqrestore(&m_lock, flags);

        if (port->tfd & (ATA_DEV_ERR | ATA_DEV_BUSY)) {
            return CHIMERA_ERR_IO;
        }

        return (port->ci & 1u) ? CHIMERA_ERR_IO : CHIMERA_SUCCESS;
    }

    // mirrors read_blocks but with ATA_CMD_WRITE_DMA_EXT and w=1
    chimera_error_t write_blocks(u32 port_idx, u64 lba, u32 count, const void *buffer) {
        if (!m_hba || count == 0 || !buffer) return CHIMERA_ERR_INVALID;

        hba_port_t *port = nullptr;
        bool found = false;
        for (u32 i = 0; i < m_port_count; i++) {
            if (m_active_ports[i] == port_idx) {
                port = &m_hba->ports[port_idx];
                found = true;
                break;
            }
        }
        if (!found || !port) return CHIMERA_ERR_NOTFOUND;

        irq_flags_t flags = spinlock_lock_irqsave(&m_lock);

        port->is   = (u32)-1;
        port->serr = (u32)-1;

        u64 clb_phys = ((u64)port->clbu << 32) | port->clb;
        hba_cmd_header_t *cmdheader = (hba_cmd_header_t *)(clb_phys + g_hhdm_base);

        cmdheader->cfl   = sizeof(fis_reg_h2d_t) / sizeof(u32);
        cmdheader->w     = 1; // write direction
        cmdheader->prdtl = 1;

        u64 ctba_phys = ((u64)cmdheader->ctbau << 32) | cmdheader->ctba;
        hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t *)(ctba_phys + g_hhdm_base);
        __builtin_memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t));

        // buffer physical address
        u64 buf_vaddr = (u64)buffer;
        u64 buf_phys = pmap_vtophys(pmap_kernel_pml4(), buf_vaddr);
        if (!buf_phys) {
            spinlock_unlock_irqrestore(&m_lock, flags);
            return CHIMERA_ERR_INVALID;
        }

        cmdtbl->prdt_entry[0].dba  = (u32)(buf_phys & 0xFFFFFFFF);
        cmdtbl->prdt_entry[0].dbau = (u32)((buf_phys >> 32) & 0xFFFFFFFF);
        cmdtbl->prdt_entry[0].dbc  = (count * 512) - 1;
        cmdtbl->prdt_entry[0].i    = 1;

        fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t *)(&cmdtbl->cfis[0]);
        cmdfis->fis_type = FIS_TYPE_REG_H2D;
        cmdfis->c        = 1;
        cmdfis->command  = 0x35; // ATA_CMD_WRITE_DMA_EXT

        cmdfis->lba0 = (u8)lba;
        cmdfis->lba1 = (u8)(lba >> 8);
        cmdfis->lba2 = (u8)(lba >> 16);
        cmdfis->device = 1u << 6;
        cmdfis->lba3 = (u8)(lba >> 24);
        cmdfis->lba4 = (u8)(lba >> 32);
        cmdfis->lba5 = (u8)(lba >> 40);
        cmdfis->countl = (u8)(count & 0xFF);
        cmdfis->counth = (u8)((count >> 8) & 0xFF);

        port->ci = 1u;

        u32 timeout = 1000000;
        while (timeout--) {
            if (!(port->ci & 1u)) break;
            if (port->is & (1u << 30)) {
                spinlock_unlock_irqrestore(&m_lock, flags);
                return CHIMERA_ERR_IO;
            }
        }

        spinlock_unlock_irqrestore(&m_lock, flags);

        if (port->tfd & (ATA_DEV_ERR | ATA_DEV_BUSY)) return CHIMERA_ERR_IO;
        return (port->ci & 1u) ? CHIMERA_ERR_IO : CHIMERA_SUCCESS;
    }

private:
    void stop_cmd(hba_port_t *port) {
        port->cmd &= ~HBA_CMD_ST;
        port->cmd &= ~HBA_CMD_FRE;

        u32 timeout = 500000;
        while (timeout--) {
            if (!(port->cmd & HBA_CMD_FR) && !(port->cmd & HBA_CMD_CR)) break;
        }
    }

    void start_cmd(hba_port_t *port) {
        u32 timeout = 500000;
        while (timeout--) {
            if (!(port->cmd & HBA_CMD_CR)) break;
        }
        port->cmd |= HBA_CMD_FRE;
        port->cmd |= HBA_CMD_ST;
    }

    void rebase_port(hba_port_t *port, u32 port_no) {
        (void)port_no;
        stop_cmd(port);

        chimera_paddr_t page_phys = pmm_alloc_page();
        if (page_phys == (chimera_paddr_t)-1 || page_phys == 0) return;

        u8 *virt = (u8 *)(page_phys + g_hhdm_base);
        __builtin_memset(virt, 0, 4096);

        // 1KB Command List (32 headers * 32 bytes)
        port->clb  = (u32)(page_phys & 0xFFFFFFFF);
        port->clbu = (u32)((page_phys >> 32) & 0xFFFFFFFF);

        // 256B Received FIS area
        chimera_paddr_t fis_phys = page_phys + 1024;
        port->fb  = (u32)(fis_phys & 0xFFFFFFFF);
        port->fbu = (u32)((fis_phys >> 32) & 0xFFFFFFFF);

        // Command Table for slot 0 at offset 2048
        hba_cmd_header_t *cmdheader = (hba_cmd_header_t *)virt;
        chimera_paddr_t ct_phys = page_phys + 2048;
        cmdheader[0].ctba  = (u32)(ct_phys & 0xFFFFFFFF);
        cmdheader[0].ctbau = (u32)((ct_phys >> 32) & 0xFFFFFFFF);

        start_cmd(port);
    }

    u64        m_abar_phys;
    hba_mem_t *m_hba;
    spinlock_t m_lock;
    u32        m_active_ports[32];
    u32        m_port_count;
};

static AHCIDriver *s_ahci_driver_instance = nullptr;

} // namespace XIUKit

inline void* operator new(unsigned long, void* p) noexcept { return p; }

extern "C" void chimerakit_ahci_init(u64 abar_phys) {
    if (!abar_phys) return;
    alignas(XIUKit::AHCIDriver) static char ahci_buf[sizeof(XIUKit::AHCIDriver)];
    static bool initialized = false;
    if (!initialized) {
        XIUKit::AHCIDriver* s_ahci = new (ahci_buf) XIUKit::AHCIDriver(abar_phys);
        s_ahci->init();
        XIUKit::s_ahci_driver_instance = s_ahci;
        initialized = true;
    }
}

extern "C" chimera_error_t ahci_read_blocks(u32 port, u64 lba, u32 count, void *buffer) {
    if (!XIUKit::s_ahci_driver_instance) return CHIMERA_ERR_NOTFOUND;
    return XIUKit::s_ahci_driver_instance->read_blocks(port, lba, count, buffer);
}

extern "C" chimera_error_t ahci_write_blocks(u32 port, u64 lba, u32 count, const void *buffer) {
    if (!XIUKit::s_ahci_driver_instance) return CHIMERA_ERR_NOTFOUND;
    return XIUKit::s_ahci_driver_instance->write_blocks(port, lba, count, buffer);
}

