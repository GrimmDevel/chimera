/* =============================================================================
 * XIU Operating System — Virtual File System & Root Hierarchy
 * kernel/vfs/vfs.c
 * ============================================================================= */

#include <kernel/vfs_node.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>

vnode_t *vfs_root_vnode = nullptr;

/* ── Flat vnode registry ─────────────────────────────────────────────────── *
 * Open-addressing hash table: path string → vnode pointer.
 * Sized for Stage 2: max 128 entries (devfs, synthetics, a few app vnodes).
 * The table is append-only — vnodes are never removed in Stage 2.
 *
 * Key insight: we don't need a full pathname resolution engine yet.
 * vfs_lookup() splits the path into components but checks the full path
 * first as a fast-path for absolute device paths (/dev/fb0, etc.).
 * ─────────────────────────────────────────────────────────────────────────── */
#define VFS_REGISTRY_SIZE   128

typedef struct vfs_entry {
    char      ve_path[256];
    vnode_t  *ve_vnode;
} vfs_entry_t;

static vfs_entry_t  s_registry[VFS_REGISTRY_SIZE];
static spinlock_t   s_registry_lock = SPINLOCK_INIT;

// djb2 hash over the path string
static u32 vfs_hash(const char *path) {
    u32 h = 5381;
    while (*path) h = ((h << 5) + h) ^ (u8)(*path++);
    return h;
}

// vfs_register
xiu_error_t vfs_register(const char *path, vnode_t *vp) {
    XIU_ASSERT(path != nullptr);
    XIU_ASSERT(vp   != nullptr);

    irq_flags_t irq = spinlock_lock_irqsave(&s_registry_lock);

    u32 idx = vfs_hash(path) % VFS_REGISTRY_SIZE;
    u32 probe = 0;

    while (probe < VFS_REGISTRY_SIZE) {
        vfs_entry_t *e = &s_registry[idx];
        if (e->ve_vnode == nullptr) {
            // empty slot — insert here
            __builtin_strncpy(e->ve_path, path, 255);
            e->ve_path[255] = '\0';
            e->ve_vnode = vp;
            spinlock_unlock_irqrestore(&s_registry_lock, irq);
            return XIU_SUCCESS;
        }
        if (__builtin_strcmp(e->ve_path, path) == 0) {
            // already registered — update
            e->ve_vnode = vp;
            spinlock_unlock_irqrestore(&s_registry_lock, irq);
            return XIU_SUCCESS;
        }
        idx = (idx + 1) % VFS_REGISTRY_SIZE;
        probe++;
    }

    spinlock_unlock_irqrestore(&s_registry_lock, irq);
    kprintf("[vfs] vfs_register: registry full, cannot register '%s'\n", path);
    return XIU_ERR_OVERFLOW;
}

/* ── vfs_lookup ──────────────────────────────────────────────────────────── *
 * Resolve an absolute path to a vnode.
 *
 * Fast-path: exact match in the hash table (O(1) average).
 * Returns XIU_SUCCESS with *vp_out set, or XIU_ERR_NOENT.
 * ─────────────────────────────────────────────────────────────────────────── */
xiu_error_t vfs_lookup(const char *path, vnode_t **vp_out) {
    XIU_ASSERT(path   != nullptr);
    XIU_ASSERT(vp_out != nullptr);

    if (__builtin_strcmp(path, ".") == 0) {
        path = "/";
    }

    irq_flags_t irq = spinlock_lock_irqsave(&s_registry_lock);

    u32 idx = vfs_hash(path) % VFS_REGISTRY_SIZE;
    u32 probe = 0;

    while (probe < VFS_REGISTRY_SIZE) {
        vfs_entry_t *e = &s_registry[idx];
        if (e->ve_vnode == nullptr) {
            break;  // empty slot = not found
        }
        if (__builtin_strcmp(e->ve_path, path) == 0) {
            *vp_out = e->ve_vnode;
            spinlock_unlock_irqrestore(&s_registry_lock, irq);
            return XIU_SUCCESS;
        }
        idx = (idx + 1) % VFS_REGISTRY_SIZE;
        probe++;
    }

    spinlock_unlock_irqrestore(&s_registry_lock, irq);
    return XIU_ERR_NOTFOUND;
}

const char *vfs_path_for_vnode(vnode_t *vp) {
    if (!vp) return nullptr;
    for (u32 i = 0; i < VFS_REGISTRY_SIZE; i++) {
        if (s_registry[i].ve_vnode == vp)
            return s_registry[i].ve_path;
    }
    return nullptr;
}

xiu_error_t vfs_readdir_flat(vnode_t *dvp, u32 index, char *name_out,
                             usize name_cap, vnode_t **child_out) {
    const char *dir = vfs_path_for_vnode(dvp);
    if (!dir || !name_out || name_cap == 0 || !child_out)
        return XIU_ERR_INVALID;

    usize dir_len = __builtin_strlen(dir);
    bool root = (dir_len == 1 && dir[0] == '/');
    u32 seen = 0;

    for (u32 i = 0; i < VFS_REGISTRY_SIZE; i++) {
        const char *path = s_registry[i].ve_path;
        vnode_t *vp = s_registry[i].ve_vnode;
        if (!vp || path[0] == '\0')
            continue;
        if (__builtin_strcmp(path, dir) == 0)
            continue;

        const char *rest = nullptr;
        if (root) {
            if (path[0] != '/')
                continue;
            rest = path + 1;
        } else {
            bool prefix_match = true;
            for (usize j = 0; j < dir_len; j++) {
                if (path[j] != dir[j]) {
                    prefix_match = false;
                    break;
                }
            }
            if (!prefix_match || path[dir_len] != '/')
                continue;
            rest = path + dir_len + 1;
        }

        if (!rest || rest[0] == '\0')
            continue;
        bool direct = true;
        for (const char *p = rest; *p; p++) {
            if (*p == '/') {
                direct = false;
                break;
            }
        }
        if (!direct)
            continue;

        if (seen++ != index)
            continue;

        __builtin_strncpy(name_out, rest, name_cap - 1);
        name_out[name_cap - 1] = '\0';
        *child_out = vp;
        return XIU_SUCCESS;
    }

    return XIU_ERR_NOTFOUND;
}

// static vnode pool for synthetic directories
#define SYNTH_DIR_POOL_SIZE 64
static vnode_t     s_synth_pool[SYNTH_DIR_POOL_SIZE];
static u32         s_synth_count = 0;
static vnode_ops_t s_root_ops    = { .vop_name = "rootfs" };
static vnode_ops_t s_synthdir_ops = { .vop_name = "synthdir" };
static vnode_t     s_root_vnode_obj;

static xiu_error_t create_synthetic_dir(const char *name) {
    if (s_synth_count >= SYNTH_DIR_POOL_SIZE) {
        kprintf("[vfs] WARNING: synth dir pool exhausted, skipping /%s\n", name);
        return XIU_ERR_OVERFLOW;
    }

    vnode_t *vp = &s_synth_pool[s_synth_count++];
    __builtin_memset(vp, 0, sizeof(vnode_t));
    vp->v_signature = XIU_VNODE_MAGIC;
    vp->v_type      = VDIR;
    vp->v_flags     = VN_SYSTEM;
    vp->v_op        = &s_synthdir_ops;
    __builtin_strncpy(vp->v_name, name, sizeof(vp->v_name) - 1);

    // build and register the full path
    char fullpath[260];
    fullpath[0] = '/';
    __builtin_strncpy(fullpath + 1, name, 254);
    fullpath[255] = '\0';

    kprintf("        vfs: creating /%s...\n", name);
    return vfs_register(fullpath, vp);
}

xiu_error_t vfs_register_module(const char *path, void *addr, usize size) {
    if (s_synth_count >= SYNTH_DIR_POOL_SIZE) return XIU_ERR_OVERFLOW;
    
    // strip drive prefixes if present
    const char *p = path;
    for (const char *c = path; *c != '\0'; c++) {
        if (*c == ':') {
            p = c + 1;
            break;
        }
    }
    char norm_path[256];
    if (p[0] != '/') {
        norm_path[0] = '/';
        __builtin_strncpy(norm_path + 1, p, 254);
    } else {
        __builtin_strncpy(norm_path, p, 255);
    }
    norm_path[255] = '\0';

    vnode_t *vp = &s_synth_pool[s_synth_count++];
    __builtin_memset(vp, 0, sizeof(vnode_t));
    vp->v_signature = XIU_VNODE_MAGIC;
    vp->v_type      = VREG;
    vp->v_flags     = VN_SYSTEM;
    vp->v_op        = &s_root_ops; // modules are read-only blocks
    vp->v_data      = addr;
    vp->v_attr.va_size = size;
    
    // copy name from last component of path
    const char *name = norm_path;
    for(const char *s = norm_path; *s; s++) if(*s == '/') name = s + 1;
    __builtin_strncpy(vp->v_name, name, sizeof(vp->v_name) - 1);

    return vfs_register(norm_path, vp);
}

xiu_error_t vfs_init(void) {
    __builtin_memset(s_registry, 0, sizeof(s_registry));

    // create and register root vnode
    vfs_root_vnode = &s_root_vnode_obj;
    __builtin_memset(vfs_root_vnode, 0, sizeof(vnode_t));
    vfs_root_vnode->v_signature = XIU_VNODE_MAGIC;
    vfs_root_vnode->v_type      = VDIR;
    vfs_root_vnode->v_flags     = VN_ROOT | VN_SYSTEM;
    vfs_root_vnode->v_op        = &s_root_ops;
    __builtin_strncpy(vfs_root_vnode->v_name, "/", 2);

    vfs_register("/", vfs_root_vnode);
    return XIU_SUCCESS;
}

extern void devfs_init(void);

xiu_error_t vfs_build_root_hierarchy(void) {
    create_synthetic_dir("bin");
    create_synthetic_dir("sbin");
    create_synthetic_dir("etc");
    create_synthetic_dir("dev");
    create_synthetic_dir("tmp");
    create_synthetic_dir("Users");
    create_synthetic_dir("System");
    create_synthetic_dir("Library");
    create_synthetic_dir("Volumes");
    create_synthetic_dir("private");

    devfs_init();
    return XIU_SUCCESS;
}
