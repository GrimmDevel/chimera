/* xiu shim: satisfy ravynos sudo sha2.c config.h expectations */
#ifndef _XIU_CONFIG_H_
#define _XIU_CONFIG_H_

#define HAVE_STDINT_H     1
#define HAVE_SYS_ENDIAN_H 1

/* xiu is x86_64 little-endian */
#ifndef BYTE_ORDER
#define LITTLE_ENDIAN 1234
#define BIG_ENDIAN    4321
#define BYTE_ORDER    LITTLE_ENDIAN
#endif

#endif
