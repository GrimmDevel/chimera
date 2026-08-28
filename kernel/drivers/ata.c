/* =============================================================================
 * Chimera Operating System — ATA / IDE Primary Master Block Driver
 * kernel/drivers/ata.c
 * ============================================================================= */

#include <kernel/ata.h>
#include <kernel/io.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>

extern void kprintf(const char *fmt, ...);

#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_ERR_FEATURES 0x1F1
#define ATA_PRIMARY_SEC_COUNT    0x1F2
#define ATA_PRIMARY_LBA_LO       0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HI       0x1F5
#define ATA_PRIMARY_DRIVE_HEAD   0x1F6
#define ATA_PRIMARY_STATUS_CMD   0x1F7
#define ATA_PRIMARY_CONTROL      0x3F6

#define ATA_STATUS_ERR 0x01
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_SRV 0x10
#define ATA_STATUS_DF  0x20
#define ATA_STATUS_RDY 0x40
#define ATA_STATUS_BSY 0x80

#define ATA_CMD_READ_SECTORS     0x20
#define ATA_CMD_WRITE_SECTORS    0x30
#define ATA_CMD_READ_SECTORS_EXT 0x24
#define ATA_CMD_WRITE_SECTORS_EXT 0x34
#define ATA_CMD_IDENTIFY         0xEC
#define ATA_CMD_FLUSH_CACHE      0xE7

ata_device_t g_ata_drive;
static spinlock_t s_ata_lock = SPINLOCK_INIT;

static void ata_400ns_delay(void) {
    for (int i = 0; i < 4; i++) {
        (void)inb(ATA_PRIMARY_CONTROL);
    }
}

static bool ata_wait_bsy_clear(void) {
    u32 timeout = 1000000;
    while (timeout--) {
        u8 status = inb(ATA_PRIMARY_STATUS_CMD);
        if (!(status & ATA_STATUS_BSY)) {
            return true;
        }
    }
    return false;
}

static bool ata_wait_drq(void) {
    u32 timeout = 1000000;
    while (timeout--) {
        u8 status = inb(ATA_PRIMARY_STATUS_CMD);
        if (status & ATA_STATUS_ERR) {
            return false;
        }
        if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ)) {
            return true;
        }
    }
    return false;
}

chimera_error_t ata_init(void) {
    irq_flags_t irq = spinlock_lock_irqsave(&s_ata_lock);

    __builtin_memset(&g_ata_drive, 0, sizeof(ata_device_t));

    // select Primary Master
    outb(ATA_PRIMARY_DRIVE_HEAD, 0xA0);
    ata_400ns_delay();

    // disable IRQs on ATA control port for pure PIO polling
    outb(ATA_PRIMARY_CONTROL, 0x02);

    // zero sector count and LBA registers
    outb(ATA_PRIMARY_SEC_COUNT, 0);
    outb(ATA_PRIMARY_LBA_LO, 0);
    outb(ATA_PRIMARY_LBA_MID, 0);
    outb(ATA_PRIMARY_LBA_HI, 0);

    // send IDENTIFY command
    outb(ATA_PRIMARY_STATUS_CMD, ATA_CMD_IDENTIFY);
    ata_400ns_delay();

    u8 status = inb(ATA_PRIMARY_STATUS_CMD);
    if (status == 0 || status == 0xFF) {
        // no device attached
        spinlock_unlock_irqrestore(&s_ata_lock, irq);
        kprintf("[ATA] No Primary Master drive detected.\n");
        return CHIMERA_ERR_NOTFOUND;
    }

    if (!ata_wait_bsy_clear()) {
        spinlock_unlock_irqrestore(&s_ata_lock, irq);
        kprintf("[ATA] Drive timeout during identify.\n");
        return CHIMERA_ERR_TIMEOUT;
    }

    u8 mid = inb(ATA_PRIMARY_LBA_MID);
    u8 hi  = inb(ATA_PRIMARY_LBA_HI);
    if (mid != 0 || hi != 0) {
        spinlock_unlock_irqrestore(&s_ata_lock, irq);
        kprintf("[ATA] Non-ATA device (ATAPI) detected, skipping.\n");
        return CHIMERA_ERR_NOTSUP;
    }

    if (!ata_wait_drq()) {
        spinlock_unlock_irqrestore(&s_ata_lock, irq);
        kprintf("[ATA] DRQ not set after IDENTIFY.\n");
        return CHIMERA_ERR_GENERIC;
    }

    u16 ident[256];
    for (int i = 0; i < 256; i++) {
        ident[i] = inw(ATA_PRIMARY_DATA);
    }

    g_ata_drive.present = true;

    // model string
    for (int i = 0; i < 20; i++) {
        u16 w = ident[27 + i];
        g_ata_drive.model[i * 2]     = (char)(w >> 8);
        g_ata_drive.model[i * 2 + 1] = (char)(w & 0xFF);
    }
    g_ata_drive.model[40] = '\0';
    for (int i = 39; i >= 0 && g_ata_drive.model[i] == ' '; i--) {
        g_ata_drive.model[i] = '\0';
    }

    // check lba48 support
    if (ident[83] & (1 << 10)) {
        g_ata_drive.lba48_supported = true;
        u64 sec = (u64)ident[100] |
                  ((u64)ident[101] << 16) |
                  ((u64)ident[102] << 32) |
                  ((u64)ident[103] << 48);
        g_ata_drive.sector_count = sec;
    } else {
        g_ata_drive.lba48_supported = false;
        // words 60..61 contain 28-bit sector count
        g_ata_drive.sector_count = (u64)ident[60] | ((u64)ident[61] << 16);
    }

    spinlock_unlock_irqrestore(&s_ata_lock, irq);

    u64 size_mb = (g_ata_drive.sector_count * ATA_SECTOR_SIZE) / (1024 * 1024);
    kprintf("[ATA] Detected Primary Master drive: \"%s\" (%llu MiB, %llu sectors)\n",
            g_ata_drive.model,
            (unsigned long long)size_mb,
            (unsigned long long)g_ata_drive.sector_count);

    return CHIMERA_SUCCESS;
}

bool ata_is_present(void) {
    return g_ata_drive.present;
}

u64 ata_get_sector_count(void) {
    return g_ata_drive.sector_count;
}

static chimera_error_t ata_read_single_sector_lba28(u32 lba, void *buf) {
    if (!ata_wait_bsy_clear()) return CHIMERA_ERR_TIMEOUT;

    outb(ATA_PRIMARY_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SEC_COUNT, 1);
    outb(ATA_PRIMARY_LBA_LO, (u8)lba);
    outb(ATA_PRIMARY_LBA_MID, (u8)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (u8)(lba >> 16));
    outb(ATA_PRIMARY_STATUS_CMD, ATA_CMD_READ_SECTORS);

    if (!ata_wait_drq()) return CHIMERA_ERR_GENERIC;

    u16 *ptr = (u16 *)buf;
    for (int i = 0; i < 256; i++) {
        ptr[i] = inw(ATA_PRIMARY_DATA);
    }
    return CHIMERA_SUCCESS;
}

static chimera_error_t ata_write_single_sector_lba28(u32 lba, const void *buf) {
    if (!ata_wait_bsy_clear()) return CHIMERA_ERR_TIMEOUT;

    outb(ATA_PRIMARY_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SEC_COUNT, 1);
    outb(ATA_PRIMARY_LBA_LO, (u8)lba);
    outb(ATA_PRIMARY_LBA_MID, (u8)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (u8)(lba >> 16));
    outb(ATA_PRIMARY_STATUS_CMD, ATA_CMD_WRITE_SECTORS);

    if (!ata_wait_drq()) return CHIMERA_ERR_GENERIC;

    const u16 *ptr = (const u16 *)buf;
    for (int i = 0; i < 256; i++) {
        outw(ATA_PRIMARY_DATA, ptr[i]);
    }

    outb(ATA_PRIMARY_STATUS_CMD, ATA_CMD_FLUSH_CACHE);
    ata_wait_bsy_clear();

    return CHIMERA_SUCCESS;
}

chimera_error_t ata_read_sectors(u64 lba, u32 count, void *buf) {
    if (!g_ata_drive.present) return CHIMERA_ERR_NOTFOUND;
    if (lba + count > g_ata_drive.sector_count) return CHIMERA_ERR_OVERFLOW;

    irq_flags_t irq = spinlock_lock_irqsave(&s_ata_lock);

    u8 *dst = (u8 *)buf;
    for (u32 i = 0; i < count; i++) {
        chimera_error_t err = ata_read_single_sector_lba28((u32)(lba + i), dst + (i * ATA_SECTOR_SIZE));
        if (err != CHIMERA_SUCCESS) {
            spinlock_unlock_irqrestore(&s_ata_lock, irq);
            return err;
        }
    }

    spinlock_unlock_irqrestore(&s_ata_lock, irq);
    return CHIMERA_SUCCESS;
}

chimera_error_t ata_write_sectors(u64 lba, u32 count, const void *buf) {
    if (!g_ata_drive.present) return CHIMERA_ERR_NOTFOUND;
    if (lba + count > g_ata_drive.sector_count) return CHIMERA_ERR_OVERFLOW;

    irq_flags_t irq = spinlock_lock_irqsave(&s_ata_lock);

    const u8 *src = (const u8 *)buf;
    for (u32 i = 0; i < count; i++) {
        chimera_error_t err = ata_write_single_sector_lba28((u32)(lba + i), src + (i * ATA_SECTOR_SIZE));
        if (err != CHIMERA_SUCCESS) {
            spinlock_unlock_irqrestore(&s_ata_lock, irq);
            return err;
        }
    }

    spinlock_unlock_irqrestore(&s_ata_lock, irq);
    return CHIMERA_SUCCESS;
}
