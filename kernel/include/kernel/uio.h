// uio buffer move
#ifndef XIU_UIO_H
#define XIU_UIO_H

#include <kernel/xiu_types.h>

typedef enum {
    UIO_READ,
    UIO_WRITE
} uio_rw_t;

typedef enum {
    UIO_USERSPACE,
    UIO_SYSSPACE
} uio_seg_t;

struct uio {
    void     *uio_buf;
    usize     uio_resid;
    u64       uio_offset;
    uio_seg_t uio_segflg;
    uio_rw_t  uio_rw;
};

static inline xiu_error_t uio_move(void *cp, usize n, struct uio *uio) {
    if (uio->uio_resid < n) n = uio->uio_resid;
    
    if (uio->uio_rw == UIO_READ) {
        __builtin_memcpy(uio->uio_buf, cp, n);
    } else {
        __builtin_memcpy(cp, uio->uio_buf, n);
    }
    
    uio->uio_buf = (char *)uio->uio_buf + n;
    uio->uio_resid -= n;
    uio->uio_offset += n;
    return 0;
}

#endif
