/*
 * Chimera Operating System — PAM Interface Header
 * Compatible with OpenPAM and Linux-PAM
 */

#ifndef _SECURITY_PAM_APPL_H_
#define _SECURITY_PAM_APPL_H_

#include <sys/cdefs.h>
#include <stddef.h>

#define PAM_SUCCESS                 0
#define PAM_OPEN_ERR                1
#define PAM_SYMBOL_ERR              2
#define PAM_SERVICE_ERR             3
#define PAM_SYSTEM_ERR              4
#define PAM_BUF_ERR                 5
#define PAM_PERM_DENIED             6
#define PAM_AUTH_ERR                7
#define PAM_AUTHINFO_UNAVAIL        9
#define PAM_USER_UNKNOWN            10
#define PAM_MAXTRIES                11
#define PAM_AUTHTOK_ERR             12
#define PAM_AUTHTOK_RECOVERY_ERR    13
#define PAM_AUTHTOK_LOCK_BUSY       14
#define PAM_AUTHTOK_DISABLE_AGING   15
#define PAM_NO_MODULE_DATA          16
#define PAM_IGNORE                  17
#define PAM_ABORT                   18
#define PAM_CONV_ERR                19
#define PAM_TRY_AGAIN               20

#define PAM_SERVICE                 1
#define PAM_USER                    2
#define PAM_TTY                     3
#define PAM_RHOST                   4
#define PAM_CONV                    5
#define PAM_AUTHTOK                 6
#define PAM_OLDAUTHTOK              7
#define PAM_RUSER                   8
#define PAM_USER_PROMPT             9

#define PAM_PROMPT_ECHO_OFF         1
#define PAM_PROMPT_ECHO_ON          2
#define PAM_ERROR_MSG               3
#define PAM_TEXT_INFO               4

#define PAM_SILENT                  0x80000000
#define PAM_DISALLOW_NULL_AUTHTOK   0x00000001
#define PAM_PRELIM_CHECK            0x00000001
#define PAM_UPDATE_AUTHTOK          0x00000002

struct pam_message {
    int msg_style;
    const char *msg;
};

struct pam_response {
    char *resp;
    int resp_retcode;
};

struct pam_conv {
    int (*conv)(int num_msg, const struct pam_message **msg,
                struct pam_response **resp, void *appdata_ptr);
    void *appdata_ptr;
};

typedef struct pam_handle pam_handle_t;

__BEGIN_DECLS

int pam_start(const char *service, const char *user,
              const struct pam_conv *pam_conv, pam_handle_t **pamh);
int pam_end(pam_handle_t *pamh, int pam_status);
int pam_set_item(pam_handle_t *pamh, int item_type, const void *item);
int pam_get_item(const pam_handle_t *pamh, int item_type, const void **item);
int pam_set_data(pam_handle_t *pamh, const char *module_data_name, void *data,
                 void (*cleanup)(pam_handle_t *pamh, void *data, int pam_end_status));
int pam_get_data(const pam_handle_t *pamh, const char *module_data_name, const void **data);
int pam_authenticate(pam_handle_t *pamh, int flags);
int pam_setcred(pam_handle_t *pamh, int flags);
int pam_acct_mgmt(pam_handle_t *pamh, int flags);
int pam_chauthtok(pam_handle_t *pamh, int flags);
int pam_open_session(pam_handle_t *pamh, int flags);
int pam_close_session(pam_handle_t *pamh, int flags);
const char *pam_strerror(const pam_handle_t *pamh, int errnum);

__END_DECLS

#endif /* !_SECURITY_PAM_APPL_H_ */
