#ifndef CHIMERA_ATA_H
#define CHIMERA_ATA_H

#include <kernel/chimera_types.h>

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

chimera_error_t ata_init(void);
bool        ata_is_present(void);
u64         ata_get_sector_count(void);
chimera_error_t ata_read_sectors(u64 lba, u32 count, void *buf);
chimera_error_t ata_write_sectors(u64 lba, u32 count, const void *buf);

#ifdef __cplusplus
}
#endif

#endif /* CHIMERA_ATA_H */
