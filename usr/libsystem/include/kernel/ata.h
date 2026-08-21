#ifndef XIU_ATA_H
#define XIU_ATA_H

#include <kernel/xiu_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ATA_SECTOR_SIZE 512

typedef struct {
    bool     present;
    bool     lba48_supported;
    u64      sector_count;
    char     model[41];
    char     serial[21];
} ata_device_t;

extern ata_device_t g_ata_drive;

xiu_error_t ata_init(void);
bool        ata_is_present(void);
u64         ata_get_sector_count(void);
xiu_error_t ata_read_sectors(u64 lba, u32 count, void *buf);
xiu_error_t ata_write_sectors(u64 lba, u32 count, const void *buf);

#ifdef __cplusplus
}
#endif

#endif /* XIU_ATA_H */
