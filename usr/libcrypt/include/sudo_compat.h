/* xiu shim: satisfy ravynos sudo sha2.h sudo_dso_public macro */
#ifndef _XIU_SUDO_COMPAT_H_
#define _XIU_SUDO_COMPAT_H_

#ifndef sudo_dso_public
#define sudo_dso_public
#endif

#ifndef __unused
#define __unused __attribute__((unused))
#endif

#endif
