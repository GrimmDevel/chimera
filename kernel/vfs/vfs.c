#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <kernel/vfs_node.h>
#include <kernel/proc.h>
#include <kernel/uio.h>

extern xiu_error_t vfs_lookup(const char *path, vnode_t **vp_out);
extern xiu_error_t vfs_register(const char *path, vnode_t *vp);

vnode_t *vfs_root_vnode = nullptr;

#define VFS_REGISTRY_SIZE 4096

typedef struct vfs_entry {
  char ve_path[256];
  vnode_t *ve_vnode;
} vfs_entry_t;

static vfs_entry_t s_registry[VFS_REGISTRY_SIZE];
static spinlock_t s_registry_lock = SPINLOCK_INIT;

static u32 vfs_hash(const char *path) {
  u32 h = 5381;
  while (*path)
    h = ((h << 5) + h) ^ (u8)(*path++);
  return h;
}

static int k_strcasecmp(const char *s1, const char *s2) {
  while (*s1 && *s2) {
    char c1 = *s1;
    char c2 = *s2;
    if (c1 >= 'A' && c1 <= 'Z')
      c1 += 32;
    if (c2 >= 'A' && c2 <= 'Z')
      c2 += 32;
    if (c1 != c2)
      return c1 - c2;
    s1++;
    s2++;
  }
  return (int)(u8)*s1 - (int)(u8)*s2;
}

static int k_strncasecmp(const char *s1, const char *s2, usize n) {
  while (n && *s1 && *s2) {
    char c1 = *s1;
    char c2 = *s2;
    if (c1 >= 'A' && c1 <= 'Z')
      c1 += 32;
    if (c2 >= 'A' && c2 <= 'Z')
      c2 += 32;
    if (c1 != c2)
      return c1 - c2;
    s1++;
    s2++;
    n--;
  }
  if (n == 0)
    return 0;
  return (int)(u8)*s1 - (int)(u8)*s2;
}

void vfs_normalize_path(const char *in, char *out, usize cap) {
  if (!in || !out || cap == 0) {
    if (out && cap > 0)
      out[0] = '\0';
    return;
  }

  const char *p = in;
  const char *colon = __builtin_strchr(p, ':');
  if (colon) {
    p = colon + 1;
  }
  if (p[0] == '(') {
    const char *close_paren = __builtin_strchr(p, ')');
    if (close_paren) {
      p = close_paren + 1;
    }
  }
  while (*p == ' ')
    p++;
  while (*p == '/' && *(p + 1) == '/')
    p++;

  char temp[256];
  usize ti = 0;

  if (p[0] != '/') {
    // prepend current working directory
    xiu_task_t *task = current_task();
    xiu_proc_t *proc = task ? task->ta_proc : nullptr;
    const char *cwd_str = nullptr;
    if (proc && proc->p_cwd) {
      if (proc->p_cwd->v_op && __builtin_strcmp(proc->p_cwd->v_op->vop_name, "fat32_dir") == 0) {
        typedef struct {
          u32 start_cluster;
          u32 file_size;
          bool is_dir;
          char path[256];
        } fat32_path_info_t;
        fat32_path_info_t *nd = (fat32_path_info_t *)proc->p_cwd->v_data;
        if (nd && nd->path[0]) cwd_str = nd->path;
      }
      if (!cwd_str && proc->p_cwd->v_name[0]) {
        cwd_str = proc->p_cwd->v_name;
      }
    }
    if (!cwd_str || cwd_str[0] == '\0') {
      cwd_str = "/";
    }

    usize clen = __builtin_strlen(cwd_str);
    for (usize i = 0; i < clen && ti + 1 < sizeof(temp); i++) {
      temp[ti++] = cwd_str[i];
    }
    if (ti == 0 || temp[ti - 1] != '/') {
      temp[ti++] = '/';
    }
  }

  while (*p && ti + 1 < sizeof(temp)) {
    if (*p == '/') {
      if (ti > 0 && temp[ti - 1] == '/') {
        p++;
        continue;
      }
    }
    temp[ti++] = *p++;
  }
  temp[ti] = '\0';

  char segs[16][64];
  int seg_count = 0;

  char *cur = temp;
  while (*cur) {
    while (*cur == '/')
      cur++;
    if (!*cur)
      break;
    char *end = cur;
    while (*end && *end != '/')
      end++;

    usize len = (usize)(end - cur);
    if (len == 1 && cur[0] == '.') {
    } else if (len == 2 && cur[0] == '.' && cur[1] == '.') {
      if (seg_count > 0)
        seg_count--;
    } else if (seg_count < 16 && len < 64) {
      __builtin_memcpy(segs[seg_count], cur, len);
      segs[seg_count][len] = '\0';
      seg_count++;
    }
    cur = end;
  }

  char reconstructed[256];
  usize ri = 0;
  reconstructed[ri++] = '/';

  for (int i = 0; i < seg_count; i++) {
    usize slen = __builtin_strlen(segs[i]);
    if (ri + slen + 1 >= sizeof(reconstructed))
      break;
    if (ri > 1)
      reconstructed[ri++] = '/';
    __builtin_memcpy(reconstructed + ri, segs[i], slen);
    ri += slen;
  }
  reconstructed[ri] = '\0';

  // darwin symlinks
  const char *final_src = reconstructed;
  char aliased[256];

  if (__builtin_strcmp(reconstructed, "/etc") == 0 ||
      k_strncasecmp(reconstructed, "/etc/", 5) == 0) {
    __builtin_strncpy(aliased, "/private/etc", sizeof(aliased) - 1);
    usize alen = __builtin_strlen(aliased);
    __builtin_strncpy(aliased + alen, reconstructed + 4, sizeof(aliased) - alen - 1);
    aliased[sizeof(aliased) - 1] = '\0';
    final_src = aliased;
  } else if (__builtin_strcmp(reconstructed, "/var") == 0 ||
             k_strncasecmp(reconstructed, "/var/", 5) == 0) {
    __builtin_strncpy(aliased, "/private/var", sizeof(aliased) - 1);
    usize alen = __builtin_strlen(aliased);
    __builtin_strncpy(aliased + alen, reconstructed + 4, sizeof(aliased) - alen - 1);
    aliased[sizeof(aliased) - 1] = '\0';
    final_src = aliased;
  } else if (__builtin_strcmp(reconstructed, "/tmp") == 0 ||
             k_strncasecmp(reconstructed, "/tmp/", 5) == 0) {
    __builtin_strncpy(aliased, "/private/tmp", sizeof(aliased) - 1);
    usize alen = __builtin_strlen(aliased);
    __builtin_strncpy(aliased + alen, reconstructed + 4, sizeof(aliased) - alen - 1);
    aliased[sizeof(aliased) - 1] = '\0';
    final_src = aliased;
  } else if (__builtin_strcmp(reconstructed, "/lib") == 0 ||
             k_strncasecmp(reconstructed, "/lib/", 5) == 0) {
    __builtin_strncpy(aliased, "/usr/lib", sizeof(aliased) - 1);
    usize alen = __builtin_strlen(aliased);
    __builtin_strncpy(aliased + alen, reconstructed + 4, sizeof(aliased) - alen - 1);
    aliased[sizeof(aliased) - 1] = '\0';
    final_src = aliased;
  }

  __builtin_strncpy(out, final_src, cap - 1);
  out[cap - 1] = '\0';
}

static vnode_ops_t s_root_ops;

#define ROOTFS_MAX_VNODES 512
static vnode_t s_rootfs_vnodes[ROOTFS_MAX_VNODES];
static u32 s_rootfs_vnode_count = 0;

static vnode_t *vfs_create_dir_vnode(const char *name) {
  if (s_rootfs_vnode_count >= ROOTFS_MAX_VNODES)
    return nullptr;
  vnode_t *vp = &s_rootfs_vnodes[s_rootfs_vnode_count++];
  __builtin_memset(vp, 0, sizeof(vnode_t));
  vp->v_signature = XIU_VNODE_MAGIC;
  vp->v_type = VDIR;
  vp->v_flags = VN_SYSTEM;
  vp->v_op = &s_root_ops;
  __builtin_strncpy(vp->v_name, name, sizeof(vp->v_name) - 1);
  return vp;
}

static void vfs_ensure_parent_dirs(const char *path) {
  if (!path || path[0] != '/')
    return;

  char buf[256];
  usize len = __builtin_strlen(path);
  if (len >= sizeof(buf))
    return;

  for (usize i = 1; i < len; i++) {
    if (path[i] == '/') {
      __builtin_memcpy(buf, path, i);
      buf[i] = '\0';

      vnode_t *existing = nullptr;
      if (vfs_lookup(buf, &existing) != XIU_SUCCESS || !existing) {
        const char *name = buf;
        for (usize j = 0; j < i; j++) {
          if (buf[j] == '/')
            name = buf + j + 1;
        }
        vnode_t *dvp = vfs_create_dir_vnode(name);
        if (dvp) {
          vfs_register(buf, dvp);
        }
      }
    }
  }
}

xiu_error_t vfs_register(const char *path, vnode_t *vp) {
  XIU_ASSERT(path != nullptr);
  XIU_ASSERT(vp != nullptr);

  char norm[256];
  vfs_normalize_path(path, norm, sizeof(norm));

  if (__builtin_strcmp(norm, "/") != 0) {
    vfs_ensure_parent_dirs(norm);
  }

  irq_flags_t irq = spinlock_lock_irqsave(&s_registry_lock);

  u32 idx = vfs_hash(norm) % VFS_REGISTRY_SIZE;
  u32 probe = 0;

  while (probe < VFS_REGISTRY_SIZE) {
    vfs_entry_t *e = &s_registry[idx];
    if (e->ve_vnode == nullptr) {
      __builtin_strncpy(e->ve_path, norm, 255);
      e->ve_path[255] = '\0';
      e->ve_vnode = vp;
      spinlock_unlock_irqrestore(&s_registry_lock, irq);
      return XIU_SUCCESS;
    }
    if (__builtin_strcmp(e->ve_path, norm) == 0) {
      e->ve_vnode = vp;
      spinlock_unlock_irqrestore(&s_registry_lock, irq);
      return XIU_SUCCESS;
    }
    idx = (idx + 1) % VFS_REGISTRY_SIZE;
    probe++;
  }

  spinlock_unlock_irqrestore(&s_registry_lock, irq);
  return XIU_ERR_OVERFLOW;
}

static void vfs_path_to_83(const char *in, char *out, usize cap) {
  if (!in || !out || cap == 0)
    return;
  usize oi = 0;
  while (*in && oi + 1 < cap) {
    if (*in == '/') {
      out[oi++] = *in++;
      continue;
    }
    const char *seg_start = in;
    while (*in && *in != '/')
      in++;
    usize seg_len = (usize)(in - seg_start);

    const char *dot = nullptr;
    for (usize i = 0; i < seg_len; i++) {
      if (seg_start[i] == '.') {
        dot = seg_start + i;
        break;
      }
    }

    usize name_len = dot ? (usize)(dot - seg_start) : seg_len;
    if (name_len > 8)
      name_len = 8;
    for (usize i = 0; i < name_len && oi + 1 < cap; i++) {
      char c = seg_start[i];
      if (c >= 'A' && c <= 'Z')
        c += 32;
      out[oi++] = c;
    }

    if (dot) {
      if (oi + 1 < cap)
        out[oi++] = '.';
      const char *ext_start = dot + 1;
      usize ext_len = (usize)((seg_start + seg_len) - ext_start);
      if (ext_len > 3)
        ext_len = 3;
      for (usize i = 0; i < ext_len && oi + 1 < cap; i++) {
        char c = ext_start[i];
        if (c >= 'A' && c <= 'Z')
          c += 32;
        out[oi++] = c;
      }
    }
  }
  out[oi] = '\0';
}

xiu_error_t vfs_lookup(const char *path, vnode_t **vp_out) {
  XIU_ASSERT(path != nullptr);
  XIU_ASSERT(vp_out != nullptr);

  char norm[256];
  vfs_normalize_path(path, norm, sizeof(norm));

  irq_flags_t irq = spinlock_lock_irqsave(&s_registry_lock);

  u32 idx = vfs_hash(norm) % VFS_REGISTRY_SIZE;
  u32 probe = 0;

  while (probe < VFS_REGISTRY_SIZE) {
    vfs_entry_t *e = &s_registry[idx];
    if (e->ve_vnode == nullptr) {
      break;
    }
    if (__builtin_strcmp(e->ve_path, norm) == 0) {
      *vp_out = e->ve_vnode;
      spinlock_unlock_irqrestore(&s_registry_lock, irq);
      return XIU_SUCCESS;
    }
    idx = (idx + 1) % VFS_REGISTRY_SIZE;
    probe++;
  }

  // hfs+/apfs case-insensitive fallback
  for (u32 i = 0; i < VFS_REGISTRY_SIZE; i++) {
    vfs_entry_t *e = &s_registry[i];
    if (e->ve_vnode && e->ve_path[0] != '\0') {
      if (k_strcasecmp(e->ve_path, norm) == 0) {
        *vp_out = e->ve_vnode;
        spinlock_unlock_irqrestore(&s_registry_lock, irq);
        return XIU_SUCCESS;
      }
    }
  }

  char path83[256];
  vfs_path_to_83(norm, path83, sizeof(path83));
  if (__builtin_strcmp(norm, path83) != 0) {
    idx = vfs_hash(path83) % VFS_REGISTRY_SIZE;
    probe = 0;
    while (probe < VFS_REGISTRY_SIZE) {
      vfs_entry_t *e = &s_registry[idx];
      if (e->ve_vnode == nullptr) {
        break;
      }
      if (__builtin_strcmp(e->ve_path, path83) == 0) {
        *vp_out = e->ve_vnode;
        spinlock_unlock_irqrestore(&s_registry_lock, irq);
        return XIU_SUCCESS;
      }
      idx = (idx + 1) % VFS_REGISTRY_SIZE;
      probe++;
    }
  }

  spinlock_unlock_irqrestore(&s_registry_lock, irq);
  return XIU_ERR_NOTFOUND;
}

const char *vfs_path_for_vnode(vnode_t *vp) {
  if (!vp)
    return nullptr;
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

    bool duplicate = false;
    for (u32 prev = 0; prev < i; prev++) {
      const char *prev_path = s_registry[prev].ve_path;
      if (!s_registry[prev].ve_vnode || prev_path[0] == '\0')
        continue;
      const char *prev_rest = nullptr;
      if (root) {
        if (prev_path[0] == '/')
          prev_rest = prev_path + 1;
      } else {
        if (__builtin_strncmp(prev_path, dir, dir_len) == 0 &&
            prev_path[dir_len] == '/')
          prev_rest = prev_path + dir_len + 1;
      }
      if (!prev_rest)
        continue;

      bool prev_direct = true;
      for (const char *p = prev_rest; *p; p++) {
        if (*p == '/') {
          prev_direct = false;
          break;
        }
      }
      if (prev_direct && __builtin_strcmp(prev_rest, rest) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate)
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

static vnode_t s_root_vnode_obj;

static xiu_error_t rootfs_vop_read(vnode_t *vp, struct uio *uio, int ioflags, vfs_context_t *ctx) {
  (void)ioflags; (void)ctx;
  if (!vp || !uio)
    return XIU_ERR_INVALID;
  if (vp->v_type == VDIR)
    return XIU_ERR_INVALID;
  if (!vp->v_data || uio->uio_offset >= vp->v_attr.va_size)
    return XIU_SUCCESS;

  usize avail = vp->v_attr.va_size - uio->uio_offset;
  usize to_copy = uio->uio_resid < avail ? uio->uio_resid : avail;

  extern xiu_error_t copyout(const void *kaddr, void *uaddr, usize len);
  xiu_error_t err = copyout((const u8 *)vp->v_data + uio->uio_offset, uio->uio_buf, to_copy);
  if (err == XIU_SUCCESS) {
    uio->uio_buf = (void *)((uptr)uio->uio_buf + to_copy);
    uio->uio_resid -= to_copy;
    uio->uio_offset += to_copy;
  }
  return err;
}

static xiu_error_t rootfs_vop_getattr(vnode_t *vp, vattr_t *vap, vfs_context_t *ctx) {
  (void)ctx;
  if (!vp || !vap)
    return XIU_ERR_INVALID;
  *vap = vp->v_attr;
  return XIU_SUCCESS;
}

static vnode_ops_t s_root_ops = {
  .vop_name = "rootfs",
  .vop_read = rootfs_vop_read,
  .vop_getattr = rootfs_vop_getattr
};

extern void devfs_init(void);

xiu_error_t vfs_register_module(const char *path, void *addr, usize size) {
  if (s_rootfs_vnode_count >= ROOTFS_MAX_VNODES)
    return XIU_ERR_OVERFLOW;

  char norm_path[256];
  vfs_normalize_path(path, norm_path, sizeof(norm_path));

  vnode_t *vp = &s_rootfs_vnodes[s_rootfs_vnode_count++];
  __builtin_memset(vp, 0, sizeof(vnode_t));
  vp->v_signature = XIU_VNODE_MAGIC;
  vp->v_type = VREG;
  vp->v_flags = VN_SYSTEM;
  vp->v_op = &s_root_ops;
  vp->v_data = addr;
  vp->v_attr.va_size = size;

  const char *name = norm_path;
  for (const char *s = norm_path; *s; s++)
    if (*s == '/')
      name = s + 1;
  __builtin_strncpy(vp->v_name, name, sizeof(vp->v_name) - 1);

  return vfs_register(norm_path, vp);
}

xiu_error_t vfs_init(void) {
  __builtin_memset(s_registry, 0, sizeof(s_registry));

  vfs_root_vnode = &s_root_vnode_obj;
  __builtin_memset(vfs_root_vnode, 0, sizeof(vnode_t));
  vfs_root_vnode->v_signature = XIU_VNODE_MAGIC;
  vfs_root_vnode->v_type = VDIR;
  vfs_root_vnode->v_flags = VN_ROOT | VN_SYSTEM;
  vfs_root_vnode->v_op = &s_root_ops;
  __builtin_strncpy(vfs_root_vnode->v_name, "/", 2);

  vfs_register("/", vfs_root_vnode);

  static const char *s_default_dirs[] = {
    "/bin",
    "/sbin",
    "/usr",
    "/usr/bin",
    "/usr/sbin",
    "/usr/lib",
    "/private",
    "/private/etc",
    "/private/var",
    "/private/tmp",
    "/Applications",
    "/Users",
    "/System",
    "/Library"
  };

  for (usize i = 0; i < sizeof(s_default_dirs) / sizeof(s_default_dirs[0]); i++) {
    const char *dpath = s_default_dirs[i];
    vnode_t *existing = nullptr;
    if (vfs_lookup(dpath, &existing) != XIU_SUCCESS || !existing) {
      const char *name = dpath;
      for (const char *s = dpath; *s; s++) {
        if (*s == '/')
          name = s + 1;
      }
      vnode_t *dvp = vfs_create_dir_vnode(name);
      if (dvp) {
        vfs_register(dpath, dvp);
      }
    }
  }

  devfs_init();
  return XIU_SUCCESS;
}
