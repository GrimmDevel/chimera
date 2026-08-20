// devfs implementation
#include <kernel/vfs_node.h>
#include <kernel/panic.h>
#include <kernel/uio.h>
#include <kernel/input.h>
#include <kernel/fb.h>
#include <kernel/io.h>

typedef struct xiu_boot_info {
    u64 magic;
    u32 version;
    u32 flags;
    xiu_paddr_t memmap_base;
    usize memmap_count;
    usize memmap_desc_size;
    xiu_paddr_t kernel_phys_start;
    xiu_paddr_t kernel_phys_end;
    xiu_vaddr_t kernel_virt_start;
    xiu_paddr_t fb_base;
    u32 fb_width;
    u32 fb_height;
    u32 fb_stride;
    u32 fb_format;
    xiu_paddr_t rsdp_base;
    xiu_paddr_t smbios_base;
    char cmdline[256];
} xiu_boot_info_t;

extern xiu_boot_info_t *g_boot_info;
extern size_t xiukit_hid_read_mouse(xiu_event_t *buf, size_t count);
extern u64 g_fb_phys_addr;

// /dev/null
static xiu_error_t dev_null_read(vnode_t *vp, struct uio *uio, int f, vfs_context_t *ctx)
    { (void)vp; (void)uio; (void)f; (void)ctx; return XIU_SUCCESS; }
static xiu_error_t dev_null_write(vnode_t *vp, struct uio *uio, int f, vfs_context_t *ctx)
    { (void)vp; (void)uio; (void)f; (void)ctx; return XIU_SUCCESS; }
static vnode_ops_t s_null_ops = { .vop_name="devfs_null",
    .vop_read=dev_null_read, .vop_write=dev_null_write };

extern i64 console_read(char *dst, usize len);
extern void console_write(const char *buf, usize len);

static xiu_error_t dev_console_read(vnode_t *vp, struct uio *uio, int f, vfs_context_t *ctx) {
    (void)vp; (void)f; (void)ctx;
    if (!uio || !uio->uio_buf || uio->uio_resid == 0) return XIU_SUCCESS;
    char tmp[256];
    usize to_read = uio->uio_resid < sizeof(tmp) ? uio->uio_resid : sizeof(tmp);
    i64 n = console_read(tmp, to_read);
    if (n <= 0) return XIU_SUCCESS;
    extern xiu_error_t copyout(const void *kaddr, void *uaddr, usize len);
    if (copyout(tmp, (void *)uio->uio_buf, n) == XIU_SUCCESS) {
        uio->uio_buf = (void *)((uptr)uio->uio_buf + n);
        uio->uio_resid -= n;
    }
    return XIU_SUCCESS;
}

static xiu_error_t dev_console_write(vnode_t *vp, struct uio *uio, int f, vfs_context_t *ctx) {
    (void)vp; (void)f; (void)ctx;
    if (!uio || !uio->uio_buf || uio->uio_resid == 0) return XIU_SUCCESS;
    char tmp[256];
    extern xiu_error_t copyin(const void *uaddr, void *kaddr, usize len);
    while (uio->uio_resid > 0) {
        usize to_write = uio->uio_resid < sizeof(tmp) ? uio->uio_resid : sizeof(tmp);
        if (copyin((const void *)uio->uio_buf, tmp, to_write) != XIU_SUCCESS)
            return XIU_ERR_GENERIC;
        console_write(tmp, to_write);
        uio->uio_buf = (void *)((uptr)uio->uio_buf + to_write);
        uio->uio_resid -= to_write;
    }
    return XIU_SUCCESS;
}

static xiu_error_t dev_console_ioctl(vnode_t *vp, u64 cmd, xiu_vaddr_t arg, vfs_context_t *ctx) {
    (void)vp; (void)ctx;
    extern xiu_error_t console_ioctl(u64 cmd, xiu_vaddr_t arg);
    return console_ioctl(cmd, arg);
}

static vnode_ops_t s_console_ops = {
    .vop_name = "devfs_console",
    .vop_read = dev_console_read,
    .vop_write = dev_console_write,
    .vop_ioctl = dev_console_ioctl
};

// /dev/fb0
static xiu_error_t dev_fb_mmap(vnode_t *vp, xiu_offset_t off, xiu_size_t sz,
                                int prot, vfs_context_t *ctx)
    { (void)vp; (void)off; (void)sz; (void)prot; (void)ctx; return XIU_SUCCESS; }

static xiu_error_t dev_fb_ioctl(vnode_t *vp, u64 cmd, xiu_vaddr_t arg, vfs_context_t *ctx) {
    (void)vp; (void)ctx;
    if (cmd == FBIOGET_INFO) {
        struct fb_info info;
        info.width = g_boot_info->fb_width;
        info.height = g_boot_info->fb_height;
        info.pitch = g_boot_info->fb_stride;
        info.bpp = 32;
        info.format = g_boot_info->fb_format;
        info.vram_size = 16 * 1024 * 1024;
        
        extern xiu_error_t copyout(const void *kaddr, void *uaddr, usize len);
        return copyout(&info, (void *)arg, sizeof(info));
    } else if (cmd == FBIOPAN_DISPLAY) {
        struct fb_pan_info pan;
        extern xiu_error_t copyin(const void *uaddr, void *kaddr, usize len);
        if (copyin((const void *)arg, &pan, sizeof(pan)) != XIU_SUCCESS) {
            return XIU_ERR_INVALID;
        }

        static bool s_virt_height_set = false;
        if (!s_virt_height_set) {
            outw(0x01CE, 0x07);
            outw(0x01CF, (u16)(g_boot_info->fb_height * 2));
            s_virt_height_set = true;
        }

        outw(0x01CE, 0x09);
        outw(0x01CF, (u16)pan.yoffset);

        return XIU_SUCCESS;
    }
    return XIU_ERR_NOTSUP;
}

static vnode_ops_t s_fb_ops = { 
    .vop_name="devfs_fb", 
    .vop_mmap=dev_fb_mmap,
    .vop_ioctl=dev_fb_ioctl
};

// /dev/serial
static xiu_error_t dev_serial_write(vnode_t *vp, struct uio *uio, int f, vfs_context_t *ctx) {
    (void)vp; (void)f; (void)ctx;
    if (!uio || !uio->uio_buf) return XIU_ERR_INVALID;
    extern void serial_puts(const char *s);
    serial_puts((const char *)uio->uio_buf);
    return XIU_SUCCESS;
}
static vnode_ops_t s_serial_ops = { .vop_name="devfs_serial",
    .vop_read=nullptr, .vop_write=dev_serial_write };

// /dev/mouse
static xiu_error_t dev_mouse_read(vnode_t *vp, struct uio *uio, int f, vfs_context_t *ctx) {
    (void)vp; (void)f; (void)ctx;
    xiu_event_t events[16];
    size_t count = xiukit_hid_read_mouse(events, 16);
    if (count == 0) return XIU_SUCCESS;
    size_t n = count * sizeof(xiu_event_t);
    if (uio->uio_resid < n) n = uio->uio_resid;
    extern xiu_error_t copyout(const void *kaddr, void *uaddr, usize len);
    if (copyout(events, uio->uio_buf, n) == XIU_SUCCESS) {
        uio->uio_buf = (char *)uio->uio_buf + n;
        uio->uio_resid -= n;
        uio->uio_offset += n;
        return XIU_SUCCESS;
    }
    return XIU_ERR_INVALID;
}
static vnode_ops_t s_mouse_ops = { .vop_name="devfs_mouse", .vop_read=dev_mouse_read };

// pty
extern vnode_ops_t s_pty_master_ops;
extern vnode_ops_t s_pty_slave_ops;
extern void pty_init(void);

// /dev/disk0
#include <kernel/ata.h>

static xiu_error_t dev_disk_read(vnode_t *vp, struct uio *uio, int f, vfs_context_t *ctx) {
    (void)vp; (void)f; (void)ctx;
    if (!uio || !uio->uio_buf || uio->uio_resid == 0) return XIU_SUCCESS;

    u64 lba = uio->uio_offset / ATA_SECTOR_SIZE;
    u64 sec_offset = uio->uio_offset % ATA_SECTOR_SIZE;
    u8 sec_buf[ATA_SECTOR_SIZE];
    extern xiu_error_t copyout(const void *kaddr, void *uaddr, usize len);

    while (uio->uio_resid > 0) {
        if (ata_read_sectors(lba, 1, sec_buf) != XIU_SUCCESS) break;
        usize avail = ATA_SECTOR_SIZE - sec_offset;
        usize to_copy = uio->uio_resid < avail ? uio->uio_resid : avail;
        if (copyout(sec_buf + sec_offset, (void *)uio->uio_buf, to_copy) != XIU_SUCCESS)
            return XIU_ERR_GENERIC;
        uio->uio_buf = (void *)((uptr)uio->uio_buf + to_copy);
        uio->uio_resid -= to_copy;
        uio->uio_offset += to_copy;
        lba++;
        sec_offset = 0;
    }
    return XIU_SUCCESS;
}

static xiu_error_t dev_disk_write(vnode_t *vp, struct uio *uio, int f, vfs_context_t *ctx) {
    (void)vp; (void)f; (void)ctx;
    if (!uio || !uio->uio_buf || uio->uio_resid == 0) return XIU_SUCCESS;

    u64 lba = uio->uio_offset / ATA_SECTOR_SIZE;
    u64 sec_offset = uio->uio_offset % ATA_SECTOR_SIZE;
    u8 sec_buf[ATA_SECTOR_SIZE];
    extern xiu_error_t copyin(const void *uaddr, void *kaddr, usize len);

    while (uio->uio_resid > 0) {
        usize avail = ATA_SECTOR_SIZE - sec_offset;
        usize to_copy = uio->uio_resid < avail ? uio->uio_resid : avail;
        if (sec_offset != 0 || to_copy < ATA_SECTOR_SIZE) {
            ata_read_sectors(lba, 1, sec_buf);
        }
        if (copyin((const void *)uio->uio_buf, sec_buf + sec_offset, to_copy) != XIU_SUCCESS)
            return XIU_ERR_GENERIC;
        if (ata_write_sectors(lba, 1, sec_buf) != XIU_SUCCESS) break;
        uio->uio_buf = (void *)((uptr)uio->uio_buf + to_copy);
        uio->uio_resid -= to_copy;
        uio->uio_offset += to_copy;
        lba++;
        sec_offset = 0;
    }
    return XIU_SUCCESS;
}

static vnode_ops_t s_disk_ops = {
    .vop_name = "devfs_disk",
    .vop_read = dev_disk_read,
    .vop_write = dev_disk_write
};

static vnode_t s_dev_dir_vnode;
static vnode_ops_t s_dev_dir_ops = { .vop_name = "devfs_dir" };
static vnode_t s_dev_null_vnode;
static vnode_t s_dev_serial_vnode;
static vnode_t s_dev_console_vnode;
static vnode_t s_dev_tty_vnode;
static vnode_t s_dev_ptmx_vnode;
static vnode_t s_dev_pts0_vnode;
static vnode_t s_dev_fb0_vnode;
static vnode_t s_dev_mouse_vnode;
static vnode_t s_dev_disk0_vnode;

extern xiu_error_t vfs_register(const char *path, vnode_t *vp);
extern xiu_error_t vfs_lookup(const char *path, vnode_t **vp_out);

static void devfs_mknode(vnode_t *vp, const char *name,
                         vtype_t type, vnode_ops_t *ops,
                         const char *devpath) {
    __builtin_memset(vp, 0, sizeof(vnode_t));
    vp->v_signature = XIU_VNODE_MAGIC;
    vp->v_type      = type;
    vp->v_flags     = VN_SYSTEM;
    vp->v_op        = ops;
    __builtin_strncpy(vp->v_name, name, sizeof(vp->v_name) - 1);
    vfs_register(devpath, vp);
}

void devfs_init(void) {
    kprintf("        devfs: mounting /dev/null, /dev/serial, /dev/console, /dev/tty, /dev/ptmx, "
            "/dev/pts/0, /dev/fb0, /dev/mouse, /dev/disk0...\n");

    pty_init();

    devfs_mknode(&s_dev_dir_vnode,     "dev",     VDIR, &s_dev_dir_ops,       "/dev");
    devfs_mknode(&s_dev_null_vnode,    "null",    VCHR, &s_null_ops,          "/dev/null");
    devfs_mknode(&s_dev_serial_vnode,  "serial",  VCHR, &s_serial_ops,        "/dev/serial");
    devfs_mknode(&s_dev_console_vnode, "console", VCHR, &s_console_ops,       "/dev/console");
    devfs_mknode(&s_dev_tty_vnode,     "tty",     VCHR, &s_console_ops,       "/dev/tty");
    devfs_mknode(&s_dev_ptmx_vnode,    "ptmx",    VCHR, &s_pty_master_ops,    "/dev/ptmx");
    devfs_mknode(&s_dev_pts0_vnode,    "pts0",    VCHR, &s_pty_slave_ops,     "/dev/pts/0");
    devfs_mknode(&s_dev_fb0_vnode,     "fb0",     VCHR, &s_fb_ops,            "/dev/fb0");
    devfs_mknode(&s_dev_mouse_vnode,   "mouse",   VCHR, &s_mouse_ops,         "/dev/mouse");
    devfs_mknode(&s_dev_disk0_vnode,   "disk0",   VBLK, &s_disk_ops,          "/dev/disk0");
}
