/* xiu shim: <sys/endian.h> for ravynos libcrypt */
#ifndef _XIU_SYS_ENDIAN_H_
#define _XIU_SYS_ENDIAN_H_

#ifndef BYTE_ORDER
#define LITTLE_ENDIAN 1234
#define BIG_ENDIAN    4321
#define BYTE_ORDER    LITTLE_ENDIAN
#endif

#endif
