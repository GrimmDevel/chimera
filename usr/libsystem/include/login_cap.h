#ifndef _LOGIN_CAP_H_
#define _LOGIN_CAP_H_

#include <sys/cdefs.h>
#include <pwd.h>

typedef void login_cap_t;

#define LOGIN_SETLOGIN          0x0001
#define LOGIN_SETUSER           0x0002
#define LOGIN_SETGROUP          0x0004
#define LOGIN_SETGROUP_PRESERVE 0x0008
#define LOGIN_SETALL            0x00ff

static inline login_cap_t *login_getpwclass(const struct passwd *pw) { (void)pw; return (login_cap_t *)0; }
static inline void login_close(login_cap_t *lc) { (void)lc; }
static inline int setusercontext(login_cap_t *lc, const struct passwd *pw, uid_t uid, unsigned int flags) {
    (void)lc; (void)pw; (void)uid; (void)flags; return 0;
}

#endif
