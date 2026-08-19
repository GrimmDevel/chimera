#ifndef XIU_FAT32_H
#define XIU_FAT32_H

#include <kernel/xiu_types.h>
#include <kernel/vfs_node.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u16 bytes_per_sector;
    u8  sectors_per_cluster;
    u16 reserved_sectors;
    u8  num_fats;
    u32 total_sectors;
    u32 sectors_per_fat;
    u32 root_cluster;
    u32 data_start_lba;
    u32 cluster_size_bytes;
    bool mounted;
} fat32_fs_t;

extern fat32_fs_t g_fat32;

xiu_error_t fat32_init(void);
xiu_error_t fat32_read_file(u32 start_cluster, u32 file_size, u32 offset, void *dst, u32 len, u32 *bytes_read);
xiu_error_t fat32_write_node(void *node_data, u32 offset, const void *src, u32 len, u32 *bytes_written);
xiu_error_t fat32_create_file(const char *path, vnode_t **out_vp);
xiu_error_t fat32_create_dir(const char *path, vnode_t **out_vp);
xiu_error_t fat32_unlink_file(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* XIU_FAT32_H */
