/* =============================================================================
 * Chimera Operating System — FAT32 Filesystem Driver (Full Read & Write)
 * kernel/vfs/fat32.c
 * ============================================================================= */

#include <kernel/fat32.h>
#include <kernel/ata.h>
#include <kernel/vfs_node.h>
#include <kernel/uio.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <kernel/proc.h>

extern void kprintf(const char *fmt, ...);
extern chimera_error_t vfs_register(const char *path, vnode_t *vp);
extern chimera_error_t vfs_lookup(const char *path, vnode_t **vp_out);
extern chimera_error_t copyout(const void *kaddr, void *uaddr, usize len);
extern chimera_error_t copyin(const void *uaddr, void *kaddr, usize len);

fat32_fs_t g_fat32;
static spinlock_t s_fat_lock = SPINLOCK_INIT;

#define FAT32_MAX_VNODES 4096

typedef struct {
    u32 start_cluster;
    u32 file_size;
    bool is_dir;
    char path[256];
    u32 parent_dir_cluster;
    u32 dir_entry_offset;  // byte offset within parent directory cluster chain
    vnode_t *vnode;
} fat32_node_data_t;

static vnode_t           s_fat_vnodes[FAT32_MAX_VNODES];
static fat32_node_data_t s_fat_data[FAT32_MAX_VNODES];
static u32               s_fat_vnode_count = 0;

// forward declarations
static vnode_ops_t s_fat32_file_ops;
static vnode_ops_t s_fat32_dir_ops;

static u32 fat32_cluster_to_lba(u32 cluster) {
    return g_fat32.data_start_lba + (cluster - 2) * g_fat32.sectors_per_cluster;
}

static u32 s_cached_fat_sec = (u32)-1;
static u8  s_cached_fat_buf[ATA_SECTOR_SIZE];

static u32 fat32_get_next_cluster_unlocked(u32 cluster) {
    if (cluster < 2 || cluster >= 0x0FFFFFF8) return 0x0FFFFFFF;

    u32 fat_offset_bytes = cluster * 4;
    u32 fat_sector = g_fat32.reserved_sectors + (fat_offset_bytes / ATA_SECTOR_SIZE);
    u32 entry_offset = fat_offset_bytes % ATA_SECTOR_SIZE;

    if (s_cached_fat_sec != fat_sector) {
        if (ata_read_sectors(fat_sector, 1, s_cached_fat_buf) != CHIMERA_SUCCESS) {
            return 0x0FFFFFFF;
        }
        s_cached_fat_sec = fat_sector;
    }

    u32 next_cluster = *(u32 *)(s_cached_fat_buf + entry_offset) & 0x0FFFFFFF;
    return next_cluster;
}

static u32 fat32_get_next_cluster(u32 cluster) {
    irq_flags_t irq = spinlock_lock_irqsave(&s_fat_lock);
    u32 next = fat32_get_next_cluster_unlocked(cluster);
    spinlock_unlock_irqrestore(&s_fat_lock, irq);
    return next;
}

static chimera_error_t fat32_set_fat_entry(u32 cluster, u32 val) {
    if (cluster < 2 || cluster >= 0x0FFFFFF8) return CHIMERA_ERR_INVALID;

    u32 fat_offset_bytes = cluster * 4;
    u32 fat_sector_idx = fat_offset_bytes / ATA_SECTOR_SIZE;
    u32 entry_offset = fat_offset_bytes % ATA_SECTOR_SIZE;

    u8 buf[ATA_SECTOR_SIZE];

    // invalidate cached sector
    s_cached_fat_sec = (u32)-1;

    // update FAT1
    u32 fat1_sec = g_fat32.reserved_sectors + fat_sector_idx;
    if (ata_read_sectors(fat1_sec, 1, buf) != CHIMERA_SUCCESS) return CHIMERA_ERR_GENERIC;
    u32 orig = *(u32 *)(buf + entry_offset);
    *(u32 *)(buf + entry_offset) = (val & 0x0FFFFFFF) | (orig & 0xF0000000);
    if (ata_write_sectors(fat1_sec, 1, buf) != CHIMERA_SUCCESS) return CHIMERA_ERR_GENERIC;

    // update FAT2
    u32 fat2_sec = g_fat32.reserved_sectors + g_fat32.sectors_per_fat + fat_sector_idx;
    if (ata_write_sectors(fat2_sec, 1, buf) != CHIMERA_SUCCESS) return CHIMERA_ERR_GENERIC;

    return CHIMERA_SUCCESS;
}

static u32 fat32_alloc_cluster(void) {
    u32 total_clusters = (g_fat32.total_sectors - g_fat32.data_start_lba) / g_fat32.sectors_per_cluster;
    u8 buf[ATA_SECTOR_SIZE];

    for (u32 sec = 0; sec < g_fat32.sectors_per_fat; sec++) {
        u32 lba = g_fat32.reserved_sectors + sec;
        if (ata_read_sectors(lba, 1, buf) != CHIMERA_SUCCESS) break;

        for (u32 off = 0; off < ATA_SECTOR_SIZE; off += 4) {
            u32 cluster = (sec * ATA_SECTOR_SIZE + off) / 4;
            if (cluster < 2 || cluster >= total_clusters + 2) continue;

            u32 entry = *(u32 *)(buf + off) & 0x0FFFFFFF;
            if (entry == 0x00000000) {
                // free cluster found! Mark as end-of-chain
                fat32_set_fat_entry(cluster, 0x0FFFFFFF);

                // zero out cluster on disk
                u8 zero_buf[512];
                __builtin_memset(zero_buf, 0, sizeof(zero_buf));
                u32 clba = fat32_cluster_to_lba(cluster);
                for (u32 s = 0; s < g_fat32.sectors_per_cluster; s++) {
                    ata_write_sectors(clba + s, 1, zero_buf);
                }

                return cluster;
            }
        }
    }
    return 0; // disk full
}

static void fat32_free_cluster_chain(u32 start_cluster) {
    u32 cluster = start_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        u32 next = fat32_get_next_cluster(cluster);
        fat32_set_fat_entry(cluster, 0x00000000);
        cluster = next;
    }
}

// name helpers
static void fat32_format_name(const u8 *entry, char *out_name, usize out_max) {
    char name[9];
    char ext[4];
    int ni = 0, ei = 0;

    for (int i = 0; i < 8; i++) {
        if (entry[i] != ' ') name[ni++] = entry[i];
    }
    name[ni] = '\0';

    for (int i = 8; i < 11; i++) {
        if (entry[i] != ' ') ext[ei++] = entry[i];
    }
    ext[ei] = '\0';

    for (int i = 0; i < ni; i++) {
        if (name[i] >= 'A' && name[i] <= 'Z') name[i] += 32;
    }
    for (int i = 0; i < ei; i++) {
        if (ext[i] >= 'A' && ext[i] <= 'Z') ext[i] += 32;
    }

    if (ei > 0) {
        int pos = 0;
        for (int i = 0; i < ni && pos < (int)out_max - 1; i++) out_name[pos++] = name[i];
        if (pos < (int)out_max - 1) out_name[pos++] = '.';
        for (int i = 0; i < ei && pos < (int)out_max - 1; i++) out_name[pos++] = ext[i];
        out_name[pos] = '\0';
    } else {
        int pos = 0;
        for (int i = 0; i < ni && pos < (int)out_max - 1; i++) out_name[pos++] = name[i];
        out_name[pos] = '\0';
    }
}

static void fat32_make_83_name(const char *in_name, u8 *out_83) {
    __builtin_memset(out_83, ' ', 11);
    char upper[64];
    usize len = __builtin_strlen(in_name);
    if (len > 63) len = 63;
    for (usize i = 0; i < len; i++) {
        char c = in_name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        upper[i] = c;
    }
    upper[len] = '\0';

    const char *dot = nullptr;
    for (int i = (int)len - 1; i >= 0; i--) {
        if (upper[i] == '.') { dot = &upper[i]; break; }
    }

    if (dot) {
        usize nlen = (usize)(dot - upper);
        if (nlen > 8) nlen = 8;
        for (usize i = 0; i < nlen; i++) out_83[i] = (u8)upper[i];

        const char *ext = dot + 1;
        usize elen = __builtin_strlen(ext);
        if (elen > 3) elen = 3;
        for (usize i = 0; i < elen; i++) out_83[8 + i] = (u8)ext[i];
    } else {
        if (len > 8) len = 8;
        for (usize i = 0; i < len; i++) out_83[i] = (u8)upper[i];
    }
}

// directory entry on-disk mutation

static chimera_error_t fat32_update_dir_entry(fat32_node_data_t *nd) {
    if (!nd || nd->parent_dir_cluster < 2) return CHIMERA_SUCCESS;

    u32 cluster = nd->parent_dir_cluster;
    u32 offset = nd->dir_entry_offset;
    u32 cluster_size = g_fat32.cluster_size_bytes;

    while (offset >= cluster_size && cluster >= 2 && cluster < 0x0FFFFFF8) {
        cluster = fat32_get_next_cluster(cluster);
        offset -= cluster_size;
    }

    if (cluster < 2 || cluster >= 0x0FFFFFF8) return CHIMERA_ERR_INVALID;

    u32 lba = fat32_cluster_to_lba(cluster) + (offset / ATA_SECTOR_SIZE);
    u32 sec_off = offset % ATA_SECTOR_SIZE;

    u8 sec[ATA_SECTOR_SIZE];
    if (ata_read_sectors(lba, 1, sec) != CHIMERA_SUCCESS) return CHIMERA_ERR_GENERIC;

    u8 *entry = sec + sec_off;
    *(u16 *)(entry + 20) = (u16)((nd->start_cluster >> 16) & 0xFFFF);
    *(u16 *)(entry + 26) = (u16)(nd->start_cluster & 0xFFFF);
    *(u32 *)(entry + 28) = nd->file_size;

    return ata_write_sectors(lba, 1, sec);
}

// read/write file data
chimera_error_t fat32_read_file(u32 start_cluster, u32 file_size, u32 offset, void *dst, u32 len, u32 *bytes_read) {
    if (bytes_read) *bytes_read = 0;
    if (!g_fat32.mounted || start_cluster < 2 || offset >= file_size || len == 0) {
        return CHIMERA_SUCCESS;
    }

    u32 to_read = len;
    if (offset + to_read > file_size) {
        to_read = file_size - offset;
    }

    u32 cluster_size = g_fat32.cluster_size_bytes;
    u32 cluster = start_cluster;

    u32 cluster_skip = offset / cluster_size;
    for (u32 i = 0; i < cluster_skip; i++) {
        cluster = fat32_get_next_cluster(cluster);
        if (cluster >= 0x0FFFFFF8 || cluster < 2) return CHIMERA_ERR_GENERIC;
    }

    u32 cluster_offset = offset % cluster_size;
    u32 read_so_far = 0;
    u8 cluster_buf[4096];

    while (read_so_far < to_read && cluster >= 2 && cluster < 0x0FFFFFF8) {
        u32 lba = fat32_cluster_to_lba(cluster);
        u32 count = g_fat32.sectors_per_cluster;
        if (count > (sizeof(cluster_buf) / ATA_SECTOR_SIZE)) {
            count = sizeof(cluster_buf) / ATA_SECTOR_SIZE;
        }

        if (ata_read_sectors(lba, count, cluster_buf) != CHIMERA_SUCCESS) return CHIMERA_ERR_GENERIC;

        u32 buf_bytes = count * ATA_SECTOR_SIZE;
        u32 avail = (buf_bytes > cluster_offset) ? (buf_bytes - cluster_offset) : 0;
        u32 chunk = (to_read - read_so_far) < avail ? (to_read - read_so_far) : avail;

        if (chunk > 0) {
            __builtin_memcpy((u8 *)dst + read_so_far, cluster_buf + cluster_offset, chunk);
            read_so_far += chunk;
        }
        cluster_offset = 0;

        if (read_so_far < to_read) {
            cluster = fat32_get_next_cluster(cluster);
        }
    }

    if (bytes_read) *bytes_read = read_so_far;
    return CHIMERA_SUCCESS;
}

chimera_error_t fat32_write_node(void *node_data, u32 offset, const void *src, u32 len, u32 *bytes_written) {
    if (bytes_written) *bytes_written = 0;
    fat32_node_data_t *nd = (fat32_node_data_t *)node_data;
    if (!nd || !g_fat32.mounted || len == 0) return CHIMERA_SUCCESS;

    irq_flags_t irq = spinlock_lock_irqsave(&s_fat_lock);

    // allocate initial cluster if empty file
    if (nd->start_cluster < 2) {
        nd->start_cluster = fat32_alloc_cluster();
        if (nd->start_cluster < 2) {
            spinlock_unlock_irqrestore(&s_fat_lock, irq);
            return CHIMERA_ERR_NORESOURCE;
        }
        fat32_update_dir_entry(nd);
    }

    u32 cluster_size = g_fat32.cluster_size_bytes;
    u32 cluster = nd->start_cluster;
    u32 prev_cluster = 0;

    // advance to target offset
    u32 cluster_skip = offset / cluster_size;
    for (u32 i = 0; i < cluster_skip; i++) {
        prev_cluster = cluster;
        cluster = fat32_get_next_cluster_unlocked(cluster);
        if (cluster >= 0x0FFFFFF8 || cluster < 2) {
            cluster = fat32_alloc_cluster();
            if (cluster < 2) {
                spinlock_unlock_irqrestore(&s_fat_lock, irq);
                return CHIMERA_ERR_NORESOURCE;
            }
            fat32_set_fat_entry(prev_cluster, cluster);
        }
    }

    u32 cluster_offset = offset % cluster_size;
    u32 written_so_far = 0;
    u8 cluster_buf[4096];

    while (written_so_far < len) {
        u32 lba = fat32_cluster_to_lba(cluster);
        u32 count = g_fat32.sectors_per_cluster;
        if (count > (sizeof(cluster_buf) / ATA_SECTOR_SIZE)) {
            count = sizeof(cluster_buf) / ATA_SECTOR_SIZE;
        }

        u32 buf_bytes = count * ATA_SECTOR_SIZE;

        // read existing cluster if partial write
        if (cluster_offset != 0 || (len - written_so_far) < buf_bytes) {
            ata_read_sectors(lba, count, cluster_buf);
        }

        u32 avail = (buf_bytes > cluster_offset) ? (buf_bytes - cluster_offset) : 0;
        u32 chunk = (len - written_so_far) < avail ? (len - written_so_far) : avail;

        if (chunk > 0) {
            __builtin_memcpy(cluster_buf + cluster_offset, (const u8 *)src + written_so_far, chunk);
        }

        if (ata_write_sectors(lba, count, cluster_buf) != CHIMERA_SUCCESS) {
            spinlock_unlock_irqrestore(&s_fat_lock, irq);
            return CHIMERA_ERR_GENERIC;
        }

        written_so_far += chunk;
        cluster_offset = 0;

        if (written_so_far < len) {
            prev_cluster = cluster;
            cluster = fat32_get_next_cluster_unlocked(cluster);
            if (cluster >= 0x0FFFFFF8 || cluster < 2) {
                cluster = fat32_alloc_cluster();
                if (cluster < 2) break;
                fat32_set_fat_entry(prev_cluster, cluster);
            }
        }
    }

    if (offset + written_so_far > nd->file_size) {
        nd->file_size = offset + written_so_far;
        if (nd->vnode) nd->vnode->v_attr.va_size = nd->file_size;
        fat32_update_dir_entry(nd);
    }

    spinlock_unlock_irqrestore(&s_fat_lock, irq);

    if (bytes_written) *bytes_written = written_so_far;
    return CHIMERA_SUCCESS;
}

// vnode ops for fat32 files

static chimera_error_t fat32_vop_read(vnode_t *vp, struct uio *uio, int flags, vfs_context_t *ctx) {
    (void)flags; (void)ctx;
    if (!vp || !uio || !uio->uio_buf || uio->uio_resid == 0) return CHIMERA_SUCCESS;

    fat32_node_data_t *data = (fat32_node_data_t *)vp->v_data;
    if (!data) return CHIMERA_ERR_INVALID;

    u32 to_read = (u32)uio->uio_resid;
    u32 offset = (u32)uio->uio_offset;

    if (offset >= data->file_size) return CHIMERA_SUCCESS;
    if (offset + to_read > data->file_size) {
        to_read = data->file_size - offset;
    }

    u8 temp_buf[4096];
    u32 total_transferred = 0;

    while (total_transferred < to_read) {
        u32 chunk = (to_read - total_transferred) < sizeof(temp_buf) ? (to_read - total_transferred) : sizeof(temp_buf);
        u32 actual = 0;
        chimera_error_t err = fat32_read_file(data->start_cluster, data->file_size, offset + total_transferred, temp_buf, chunk, &actual);
        if (err != CHIMERA_SUCCESS || actual == 0) break;

        if (copyout(temp_buf, (void *)((uptr)uio->uio_buf + total_transferred), actual) != CHIMERA_SUCCESS) {
            return CHIMERA_ERR_GENERIC;
        }

        total_transferred += actual;
    }

    uio->uio_buf = (void *)((uptr)uio->uio_buf + total_transferred);
    uio->uio_resid -= total_transferred;
    uio->uio_offset += total_transferred;

    return CHIMERA_SUCCESS;
}

static chimera_error_t fat32_vop_write(vnode_t *vp, struct uio *uio, int flags, vfs_context_t *ctx) {
    (void)flags; (void)ctx;
    if (!vp || !uio || !uio->uio_buf || uio->uio_resid == 0) return CHIMERA_SUCCESS;

    fat32_node_data_t *data = (fat32_node_data_t *)vp->v_data;
    if (!data) return CHIMERA_ERR_INVALID;

    u32 to_write = (u32)uio->uio_resid;
    u32 offset = (u32)uio->uio_offset;

    u8 temp_buf[4096];
    u32 total_written = 0;

    while (total_written < to_write) {
        u32 chunk = (to_write - total_written) < sizeof(temp_buf) ? (to_write - total_written) : sizeof(temp_buf);
        if (copyin((const void *)((uptr)uio->uio_buf + total_written), temp_buf, chunk) != CHIMERA_SUCCESS) {
            return CHIMERA_ERR_GENERIC;
        }

        u32 actual = 0;
        chimera_error_t err = fat32_write_node(data, offset + total_written, temp_buf, chunk, &actual);
        if (err != CHIMERA_SUCCESS || actual == 0) break;

        total_written += actual;
    }

    uio->uio_buf = (void *)((uptr)uio->uio_buf + total_written);
    uio->uio_resid -= total_written;
    uio->uio_offset += total_written;

    return CHIMERA_SUCCESS;
}

static chimera_error_t fat32_vop_getattr(vnode_t *vp, vattr_t *vap, vfs_context_t *ctx) {
    (void)ctx;
    if (!vp || !vap) return CHIMERA_ERR_INVALID;
    *vap = vp->v_attr;
    return CHIMERA_SUCCESS;
}

static vnode_ops_t s_fat32_file_ops = {
    .vop_name = "fat32_file",
    .vop_read = fat32_vop_read,
    .vop_write = fat32_vop_write,
    .vop_getattr = fat32_vop_getattr
};

static vnode_ops_t s_fat32_dir_ops = {
    .vop_name = "fat32_dir",
    .vop_getattr = fat32_vop_getattr
};

// node creation
static void split_parent_child_path(const char *path, char *parent, char *child) {
    usize len = __builtin_strlen(path);
    int slash_idx = -1;
    for (int i = (int)len - 1; i >= 0; i--) {
        if (path[i] == '/') { slash_idx = i; break; }
    }

    if (slash_idx <= 0) {
        parent[0] = '/'; parent[1] = '\0';
        if (slash_idx == 0) {
            __builtin_strncpy(child, path + 1, 63);
        } else {
            __builtin_strncpy(child, path, 63);
        }
    } else {
        __builtin_memcpy(parent, path, slash_idx);
        parent[slash_idx] = '\0';
        __builtin_strncpy(child, path + slash_idx + 1, 63);
    }
}

chimera_error_t fat32_create_file(const char *path, vnode_t **out_vp) {
    if (!path || !out_vp) return CHIMERA_ERR_INVALID;
    *out_vp = nullptr;

    // check if already exists
    vnode_t *existing = nullptr;
    if (vfs_lookup(path, &existing) == CHIMERA_SUCCESS && existing) {
        *out_vp = existing;
        return CHIMERA_SUCCESS;
    }

    char parent[256];
    char child[64];
    split_parent_child_path(path, parent, child);

    vnode_t *pdir_vp = nullptr;
    if (vfs_lookup(parent, &pdir_vp) != CHIMERA_SUCCESS || !pdir_vp) {
        return CHIMERA_ERR_NOTFOUND;
    }

    fat32_node_data_t *pdir_data = (fat32_node_data_t *)pdir_vp->v_data;
    u32 parent_cluster = pdir_data ? pdir_data->start_cluster : g_fat32.root_cluster;

    // find free 32-byte directory entry slot
    u32 cluster = parent_cluster;
    u32 entry_offset_in_dir = 0;
    u32 target_lba = 0;
    u32 target_sec_off = 0;
    bool found_slot = false;
    u8 cluster_buf[4096];

    while (cluster >= 2 && cluster < 0x0FFFFFF8 && !found_slot) {
        u32 lba = fat32_cluster_to_lba(cluster);
        u32 count = g_fat32.sectors_per_cluster;
        if (count > (sizeof(cluster_buf) / ATA_SECTOR_SIZE)) count = sizeof(cluster_buf) / ATA_SECTOR_SIZE;

        if (ata_read_sectors(lba, count, cluster_buf) != CHIMERA_SUCCESS) break;

        for (u32 i = 0; i < g_fat32.cluster_size_bytes; i += 32) {
            u8 *ent = cluster_buf + i;
            if (ent[0] == 0x00 || ent[0] == 0xE5) {
                target_lba = lba + (i / ATA_SECTOR_SIZE);
                target_sec_off = i % ATA_SECTOR_SIZE;
                entry_offset_in_dir += i;
                found_slot = true;
                break;
            }
        }

        if (!found_slot) {
            entry_offset_in_dir += g_fat32.cluster_size_bytes;
            u32 next = fat32_get_next_cluster(cluster);
            if (next >= 0x0FFFFFF8 || next < 2) {
                // extend directory with new cluster
                u32 new_c = fat32_alloc_cluster();
                if (new_c < 2) return CHIMERA_ERR_NORESOURCE;
                fat32_set_fat_entry(cluster, new_c);
                cluster = new_c;
                target_lba = fat32_cluster_to_lba(new_c);
                target_sec_off = 0;
                found_slot = true;
                break;
            }
            cluster = next;
        }
    }

    if (!found_slot) return CHIMERA_ERR_NORESOURCE;

    // format 8.3 entry
    u8 name_83[11];
    fat32_make_83_name(child, name_83);

    u8 sec[ATA_SECTOR_SIZE];
    if (ata_read_sectors(target_lba, 1, sec) != CHIMERA_SUCCESS) return CHIMERA_ERR_GENERIC;

    u8 *entry = sec + target_sec_off;
    __builtin_memset(entry, 0, 32);
    __builtin_memcpy(entry, name_83, 11);
    entry[11] = 0x20; // archive / File attribute
    *(u16 *)(entry + 20) = 0;
    *(u16 *)(entry + 26) = 0;
    *(u32 *)(entry + 28) = 0;

    if (ata_write_sectors(target_lba, 1, sec) != CHIMERA_SUCCESS) return CHIMERA_ERR_GENERIC;

    // allocate VFS vnode
    if (s_fat_vnode_count >= FAT32_MAX_VNODES) return CHIMERA_ERR_NORESOURCE;

    vnode_t *vp = &s_fat_vnodes[s_fat_vnode_count];
    fat32_node_data_t *nd = &s_fat_data[s_fat_vnode_count];
    s_fat_vnode_count++;

    __builtin_memset(vp, 0, sizeof(vnode_t));
    vp->v_signature = CHIMERA_VNODE_MAGIC;
    vp->v_type = VREG;
    vp->v_flags = VN_SYSTEM;
    vp->v_op = &s_fat32_file_ops;
    vp->v_attr.va_size = 0;
    __builtin_strncpy(vp->v_name, child, sizeof(vp->v_name) - 1);

    nd->start_cluster = 0;
    nd->file_size = 0;
    nd->is_dir = false;
    nd->parent_dir_cluster = parent_cluster;
    nd->dir_entry_offset = entry_offset_in_dir;
    nd->vnode = vp;
    __builtin_strncpy(nd->path, path, sizeof(nd->path) - 1);

    vp->v_data = nd;
    vfs_register(path, vp);

    *out_vp = vp;
    kprintf("[FAT32] Created file: %s (in parent cluster %u)\n", path, parent_cluster);
    return CHIMERA_SUCCESS;
}

chimera_error_t fat32_create_dir(const char *path, vnode_t **out_vp) {
    if (!path || !out_vp) return CHIMERA_ERR_INVALID;
    *out_vp = nullptr;

    char parent[256];
    char child[64];
    split_parent_child_path(path, parent, child);

    vnode_t *pdir_vp = nullptr;
    if (vfs_lookup(parent, &pdir_vp) != CHIMERA_SUCCESS || !pdir_vp) {
        return CHIMERA_ERR_NOTFOUND;
    }

    fat32_node_data_t *pdir_data = (fat32_node_data_t *)pdir_vp->v_data;
    u32 parent_cluster = pdir_data ? pdir_data->start_cluster : g_fat32.root_cluster;

    u32 new_dir_cluster = fat32_alloc_cluster();
    if (new_dir_cluster < 2) return CHIMERA_ERR_NORESOURCE;

    // write . and .. inside new directory cluster
    u8 dir_sec[ATA_SECTOR_SIZE];
    __builtin_memset(dir_sec, 0, sizeof(dir_sec));

    // .
    __builtin_memset(dir_sec, ' ', 11);
    dir_sec[0] = '.';
    dir_sec[11] = 0x10;
    *(u16 *)(dir_sec + 20) = (u16)((new_dir_cluster >> 16) & 0xFFFF);
    *(u16 *)(dir_sec + 26) = (u16)(new_dir_cluster & 0xFFFF);

    // ..
    __builtin_memset(dir_sec + 32, ' ', 11);
    dir_sec[32] = '.'; dir_sec[33] = '.';
    dir_sec[32 + 11] = 0x10;
    u32 dotdot_cluster = parent_cluster != g_fat32.root_cluster ? parent_cluster : 0;
    *(u16 *)(dir_sec + 32 + 20) = (u16)((dotdot_cluster >> 16) & 0xFFFF);
    *(u16 *)(dir_sec + 32 + 26) = (u16)(dotdot_cluster & 0xFFFF);

    u32 dlba = fat32_cluster_to_lba(new_dir_cluster);
    ata_write_sectors(dlba, 1, dir_sec);

    // add entry into parent directory
    u32 cluster = parent_cluster;
    u32 entry_offset_in_dir = 0;
    u32 target_lba = 0;
    u32 target_sec_off = 0;
    bool found_slot = false;
    u8 cluster_buf[4096];

    while (cluster >= 2 && cluster < 0x0FFFFFF8 && !found_slot) {
        u32 lba = fat32_cluster_to_lba(cluster);
        u32 count = g_fat32.sectors_per_cluster;
        if (count > (sizeof(cluster_buf) / ATA_SECTOR_SIZE)) count = sizeof(cluster_buf) / ATA_SECTOR_SIZE;

        if (ata_read_sectors(lba, count, cluster_buf) != CHIMERA_SUCCESS) break;

        for (u32 i = 0; i < g_fat32.cluster_size_bytes; i += 32) {
            u8 *ent = cluster_buf + i;
            if (ent[0] == 0x00 || ent[0] == 0xE5) {
                target_lba = lba + (i / ATA_SECTOR_SIZE);
                target_sec_off = i % ATA_SECTOR_SIZE;
                entry_offset_in_dir += i;
                found_slot = true;
                break;
            }
        }
        if (!found_slot) {
            entry_offset_in_dir += g_fat32.cluster_size_bytes;
            cluster = fat32_get_next_cluster(cluster);
        }
    }

    if (!found_slot) return CHIMERA_ERR_NORESOURCE;

    u8 name_83[11];
    fat32_make_83_name(child, name_83);

    u8 sec[ATA_SECTOR_SIZE];
    ata_read_sectors(target_lba, 1, sec);
    u8 *entry = sec + target_sec_off;
    __builtin_memset(entry, 0, 32);
    __builtin_memcpy(entry, name_83, 11);
    entry[11] = 0x10; // directory attribute
    *(u16 *)(entry + 20) = (u16)((new_dir_cluster >> 16) & 0xFFFF);
    *(u16 *)(entry + 26) = (u16)(new_dir_cluster & 0xFFFF);
    *(u32 *)(entry + 28) = 0;
    ata_write_sectors(target_lba, 1, sec);

    if (s_fat_vnode_count >= FAT32_MAX_VNODES) return CHIMERA_ERR_NORESOURCE;

    vnode_t *vp = &s_fat_vnodes[s_fat_vnode_count];
    fat32_node_data_t *nd = &s_fat_data[s_fat_vnode_count];
    s_fat_vnode_count++;

    __builtin_memset(vp, 0, sizeof(vnode_t));
    vp->v_signature = CHIMERA_VNODE_MAGIC;
    vp->v_type = VDIR;
    vp->v_flags = VN_SYSTEM;
    vp->v_op = &s_fat32_dir_ops;
    vp->v_attr.va_size = 0;
    __builtin_strncpy(vp->v_name, child, sizeof(vp->v_name) - 1);

    nd->start_cluster = new_dir_cluster;
    nd->file_size = 0;
    nd->is_dir = true;
    nd->parent_dir_cluster = parent_cluster;
    nd->dir_entry_offset = entry_offset_in_dir;
    nd->vnode = vp;
    __builtin_strncpy(nd->path, path, sizeof(nd->path) - 1);

    vp->v_data = nd;
    vfs_register(path, vp);

    *out_vp = vp;
    kprintf("[FAT32] Created directory: %s (cluster %u)\n", path, new_dir_cluster);
    return CHIMERA_SUCCESS;
}

chimera_error_t fat32_unlink_file(const char *path) {
    vnode_t *vp = nullptr;
    if (vfs_lookup(path, &vp) != CHIMERA_SUCCESS || !vp) return CHIMERA_ERR_NOTFOUND;

    fat32_node_data_t *nd = (fat32_node_data_t *)vp->v_data;
    if (!nd || nd->parent_dir_cluster < 2) return CHIMERA_ERR_INVALID;

    // mark directory entry as deleted
    u32 cluster = nd->parent_dir_cluster;
    u32 offset = nd->dir_entry_offset;
    u32 cluster_size = g_fat32.cluster_size_bytes;

    while (offset >= cluster_size && cluster >= 2 && cluster < 0x0FFFFFF8) {
        cluster = fat32_get_next_cluster(cluster);
        offset -= cluster_size;
    }

    if (cluster >= 2 && cluster < 0x0FFFFFF8) {
        u32 lba = fat32_cluster_to_lba(cluster) + (offset / ATA_SECTOR_SIZE);
        u32 sec_off = offset % ATA_SECTOR_SIZE;
        u8 sec[ATA_SECTOR_SIZE];
        if (ata_read_sectors(lba, 1, sec) == CHIMERA_SUCCESS) {
            sec[sec_off] = 0xE5; // deleted
            ata_write_sectors(lba, 1, sec);
        }
    }

    // free cluster chain
    if (nd->start_cluster >= 2) {
        fat32_free_cluster_chain(nd->start_cluster);
        nd->start_cluster = 0;
    }
    nd->file_size = 0;
    vp->v_attr.va_size = 0;

    return CHIMERA_SUCCESS;
}

static void fat32_map_canonical_name(char *name, usize max_len, bool is_dir) {
    (void)max_len;
    if (is_dir) {
        if (__builtin_strcmp(name, "applicat") == 0) __builtin_strncpy(name, "Applications", max_len - 1);
        else if (__builtin_strcmp(name, "library") == 0) __builtin_strncpy(name, "Library", max_len - 1);
        else if (__builtin_strcmp(name, "system") == 0) __builtin_strncpy(name, "System", max_len - 1);
        else if (__builtin_strcmp(name, "users") == 0) __builtin_strncpy(name, "Users", max_len - 1);
        else if (__builtin_strcmp(name, "volumes") == 0) __builtin_strncpy(name, "Volumes", max_len - 1);
        else if (__builtin_strcmp(name, "network") == 0) __builtin_strncpy(name, "Network", max_len - 1);
        else if (__builtin_strcmp(name, "coreserv") == 0) __builtin_strncpy(name, "CoreServices", max_len - 1);
        else if (__builtin_strcmp(name, "framewor") == 0) __builtin_strncpy(name, "Frameworks", max_len - 1);
        else if (__builtin_strcmp(name, "preferen") == 0) __builtin_strncpy(name, "Preferences", max_len - 1);
        else if (__builtin_strcmp(name, "shared") == 0) __builtin_strncpy(name, "Shared", max_len - 1);
        else if (__builtin_strcmp(name, "fonts") == 0) __builtin_strncpy(name, "Fonts", max_len - 1);
    } else {
        if (__builtin_strcmp(name, "sysver.pli") == 0 || __builtin_strcmp(name, "sysver") == 0)
            __builtin_strncpy(name, "SysVer.plist", max_len - 1);
        else if (__builtin_strcmp(name, "systemve.pli") == 0 || __builtin_strcmp(name, "systemve") == 0)
            __builtin_strncpy(name, "SystemVersion.plist", max_len - 1);
    }
}

typedef struct {
    char name[256];
    u8   checksum;
    bool valid;
} fat32_lfn_parser_t;

static void fat32_scan_directory(u32 dir_cluster, const char *parent_path, int depth) {
    if (depth > 12) return;
    extern chimera_paddr_t pmm_alloc_page(void);
    extern void pmm_release_page(chimera_paddr_t addr);
    
    chimera_paddr_t phys = pmm_alloc_page();
    if (!phys || phys == (chimera_paddr_t)-1) return;
    
    u8 *cluster_buf = (u8 *)(phys + g_hhdm_base);
    u32 cluster = dir_cluster;
    u32 current_offset_in_dir = 0;
    fat32_lfn_parser_t lfn;
    __builtin_memset(&lfn, 0, sizeof(lfn));

    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        u32 lba = fat32_cluster_to_lba(cluster);
        u32 count = g_fat32.sectors_per_cluster;
        if (count > (4096 / ATA_SECTOR_SIZE)) {
            count = 4096 / ATA_SECTOR_SIZE;
        }
        u32 read_bytes = count * ATA_SECTOR_SIZE;

        if (ata_read_sectors(lba, count, cluster_buf) != CHIMERA_SUCCESS) break;

        for (u32 i = 0; i < read_bytes; i += 32) {
            u8 *entry = cluster_buf + i;
            u32 entry_offset = current_offset_in_dir + i;

            if (entry[0] == 0x00) {
                pmm_release_page(phys);
                return;
            }
            if (entry[0] == 0xE5) {
                lfn.valid = false;
                continue;
            }

            if (entry[11] == 0x0F) {
                u8 seq = entry[0];
                u8 seq_num = seq & 0x1F;
                bool is_last = (seq & 0x40) != 0;
                u8 chk = entry[13];

                if (is_last) {
                    __builtin_memset(&lfn, 0, sizeof(lfn));
                    lfn.checksum = chk;
                    lfn.valid = true;
                } else if (!lfn.valid || lfn.checksum != chk) {
                    lfn.valid = false;
                    continue;
                }

                if (seq_num >= 1 && seq_num <= 20) {
                    usize base_char = (seq_num - 1) * 13;
                    for (int c = 0; c < 5; c++) {
                        u16 wc = *(u16 *)(entry + 1 + c * 2);
                        if (wc == 0x0000 || wc == 0xFFFF) break;
                        if (base_char + c < 255) lfn.name[base_char + c] = (char)(wc & 0xFF);
                    }
                    for (int c = 0; c < 6; c++) {
                        u16 wc = *(u16 *)(entry + 14 + c * 2);
                        if (wc == 0x0000 || wc == 0xFFFF) break;
                        if (base_char + 5 + c < 255) lfn.name[base_char + 5 + c] = (char)(wc & 0xFF);
                    }
                    for (int c = 0; c < 2; c++) {
                        u16 wc = *(u16 *)(entry + 28 + c * 2);
                        if (wc == 0x0000 || wc == 0xFFFF) break;
                        if (base_char + 11 + c < 255) lfn.name[base_char + 11 + c] = (char)(wc & 0xFF);
                    }
                }
                continue;
            }

            if (entry[0] == '.') {
                lfn.valid = false;
                continue;
            }

            u8 attr = entry[11];
            if (attr & 0x08) {
                lfn.valid = false;
                continue;
            }
            if ((u8)entry[0] < 0x20 || (u8)entry[0] > 0x7E) {
                lfn.valid = false;
                continue;
            }

            bool is_dir = (attr & 0x10) != 0;
            u32 start_cluster = ((u32)*(u16 *)(entry + 20) << 16) | *(u16 *)(entry + 26);
            u32 file_size = *(u32 *)(entry + 28);

            char fname[256];
            bool have_lfn = false;

            u8 short_chk = 0;
            for (int k = 0; k < 11; k++) {
                short_chk = (((short_chk & 1) << 7) + (short_chk >> 1) + entry[k]) & 0xFF;
            }

            if (lfn.valid && lfn.checksum == short_chk && lfn.name[0] != '\0') {
                __builtin_strncpy(fname, lfn.name, sizeof(fname) - 1);
                fname[sizeof(fname) - 1] = '\0';
                have_lfn = true;
            }
            lfn.valid = false;

            if (!have_lfn) {
                fat32_format_name(entry, fname, sizeof(fname));
                fat32_map_canonical_name(fname, sizeof(fname), is_dir);
            }

            if (fname[0] == '\0') continue;
            bool valid_chars = true;
            for (const char *p = fname; *p; p++) {
                if ((u8)*p < 0x20 || (u8)*p > 0x7E) {
                    valid_chars = false;
                    break;
                }
            }
            if (!valid_chars) continue;

            char full_path[256];
            if (__builtin_strcmp(parent_path, "") == 0 || __builtin_strcmp(parent_path, "/") == 0) {
                full_path[0] = '/';
                usize flen = __builtin_strlen(fname);
                for (usize k = 0; k < flen && k < 250; k++) full_path[1 + k] = fname[k];
                full_path[1 + flen] = '\0';
            } else {
                usize plen = __builtin_strlen(parent_path);
                for (usize k = 0; k < plen && k < 200; k++) full_path[k] = parent_path[k];
                full_path[plen] = '/';
                usize flen = __builtin_strlen(fname);
                for (usize k = 0; k < flen && (plen + 1 + k) < 254; k++) full_path[plen + 1 + k] = fname[k];
                full_path[plen + 1 + flen] = '\0';
            }

            if (s_fat_vnode_count < FAT32_MAX_VNODES) {
                vnode_t *vp = &s_fat_vnodes[s_fat_vnode_count];
                fat32_node_data_t *nd = &s_fat_data[s_fat_vnode_count];
                s_fat_vnode_count++;

                __builtin_memset(vp, 0, sizeof(vnode_t));
                vp->v_signature = CHIMERA_VNODE_MAGIC;
                vp->v_type = is_dir ? VDIR : VREG;
                vp->v_flags = VN_SYSTEM;
                vp->v_op = is_dir ? &s_fat32_dir_ops : &s_fat32_file_ops;
                vp->v_attr.va_size = file_size;
                __builtin_strncpy(vp->v_name, fname, sizeof(vp->v_name) - 1);

                nd->start_cluster = start_cluster;
                nd->file_size = file_size;
                nd->is_dir = is_dir;
                nd->parent_dir_cluster = dir_cluster;
                nd->dir_entry_offset = entry_offset;
                nd->vnode = vp;
                __builtin_strncpy(nd->path, full_path, sizeof(nd->path) - 1);

                vp->v_data = nd;

                vfs_register(full_path, vp);
            }

            if (is_dir && start_cluster >= 2 && start_cluster != dir_cluster && start_cluster != g_fat32.root_cluster) {
                if (__builtin_strcmp(fname, ".") != 0 && __builtin_strcmp(fname, "..") != 0 && depth < 8) {
                    bool is_cpp_tree = (full_path[0] == '/' && full_path[1] == 'u' && full_path[2] == 's' &&
                                        full_path[3] == 'r' && full_path[4] == '/' && full_path[5] == 'i' &&
                                        full_path[6] == 'n' && full_path[7] == 'c' && full_path[8] == 'l' &&
                                        full_path[9] == 'u' && full_path[10] == 'd' && full_path[11] == 'e' &&
                                        full_path[12] == '/' && full_path[13] == 'c' && full_path[14] == '+' &&
                                        full_path[15] == '+');
                    if (!is_cpp_tree) {
                        fat32_scan_directory(start_cluster, full_path, depth + 1);
                    }
                }
            }
        }

        current_offset_in_dir += g_fat32.cluster_size_bytes;
        cluster = fat32_get_next_cluster(cluster);
    }

    pmm_release_page(phys);
}

chimera_error_t fat32_init(void) {
    irq_flags_t irq = spinlock_lock_irqsave(&s_fat_lock);

    __builtin_memset(&g_fat32, 0, sizeof(fat32_fs_t));

    // ponytail: ata_read_sectors transparently falls back to AHCI when legacy ATA absent

    u8 boot_sec[ATA_SECTOR_SIZE];
    if (ata_read_sectors(0, 1, boot_sec) != CHIMERA_SUCCESS) {
        spinlock_unlock_irqrestore(&s_fat_lock, irq);
        kprintf("[FAT32] Failed to read sector 0.\n");
        return CHIMERA_ERR_GENERIC;
    }

    // check Boot Signature
    if (boot_sec[510] != 0x55 || boot_sec[511] != 0xAA) {
        spinlock_unlock_irqrestore(&s_fat_lock, irq);
        kprintf("[FAT32] Invalid boot sector signature (0x%02x, 0x%02x).\n",
                boot_sec[510], boot_sec[511]);
        return CHIMERA_ERR_INVALID;
    }

    g_fat32.bytes_per_sector = *(u16 *)(boot_sec + 11);
    g_fat32.sectors_per_cluster = boot_sec[13];
    g_fat32.reserved_sectors = *(u16 *)(boot_sec + 14);
    g_fat32.num_fats = boot_sec[16];
    g_fat32.total_sectors = *(u32 *)(boot_sec + 32);
    g_fat32.sectors_per_fat = *(u32 *)(boot_sec + 36);
    g_fat32.root_cluster = *(u32 *)(boot_sec + 44);

    if (g_fat32.bytes_per_sector != 512 || g_fat32.sectors_per_cluster == 0 ||
        g_fat32.sectors_per_cluster > 64 || g_fat32.sectors_per_fat == 0) {
        spinlock_unlock_irqrestore(&s_fat_lock, irq);
        kprintf("[FAT32] Unsupported FAT parameters (bytes/sec=%u, sec/clust=%u).\n",
                g_fat32.bytes_per_sector, g_fat32.sectors_per_cluster);
        return CHIMERA_ERR_NOTSUP;
    }

    g_fat32.data_start_lba = g_fat32.reserved_sectors + (g_fat32.num_fats * g_fat32.sectors_per_fat);
    g_fat32.cluster_size_bytes = g_fat32.sectors_per_cluster * ATA_SECTOR_SIZE;
    g_fat32.mounted = true;

    kprintf("[FAT32] Mounted FAT32 volume: total_sec=%u, sec/clust=%u, root_cluster=%u\n",
            g_fat32.total_sectors, g_fat32.sectors_per_cluster, g_fat32.root_cluster);

    spinlock_unlock_irqrestore(&s_fat_lock, irq);

    // scan and register all files and directories on disk into VFS
    fat32_scan_directory(g_fat32.root_cluster, "", 0);

    return CHIMERA_SUCCESS;
}
