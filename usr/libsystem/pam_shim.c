/*
 * Chimera Operating System — PAM Implementation
 * usr/libsystem/pam_shim.c
 *
 * Implements OpenPAM / PAM application interface using libcrypt (SHA-512/SHA-256)
 * and the /etc/master.passwd / /etc/passwd user database.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <security/pam_appl.h>
#include <security/openpam.h>

struct pam_data_entry {
    char *name;
    void *data;
    void (*cleanup)(pam_handle_t *pamh, void *data, int pam_end_status);
    struct pam_data_entry *next;
};

struct pam_handle {
    char *service;
    char *user;
    char *tty;
    char *rhost;
    char *ruser;
    char *authtok;
    char *oldauthtok;
    struct pam_conv conv;
    struct pam_data_entry *data;
};

int openpam_nullconv(int num_msg, const struct pam_message **msg,
                     struct pam_response **resp, void *data) {
    (void)num_msg; (void)msg; (void)data;
    if (resp) *resp = NULL;
    return PAM_CONV_ERR;
}

int openpam_ttyconv(int num_msg, const struct pam_message **msg,
                    struct pam_response **resp, void *data) {
    (void)data;
    if (num_msg <= 0 || !msg || !resp)
        return PAM_CONV_ERR;

    struct pam_response *r = calloc((size_t)num_msg, sizeof(struct pam_response));
    if (!r)
        return PAM_BUF_ERR;

    for (int i = 0; i < num_msg; i++) {
        const struct pam_message *m = msg[i];
        if (!m) continue;

        if (m->msg_style == PAM_PROMPT_ECHO_OFF) {
            /* Prompt without echo */
            if (m->msg) {
                fputs(m->msg, stderr);
                fflush(stderr);
            }
            struct termios orig_t, no_echo_t;
            int is_tty = isatty(STDIN_FILENO);
            if (is_tty) {
                tcgetattr(STDIN_FILENO, &orig_t);
                no_echo_t = orig_t;
                no_echo_t.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
                tcsetattr(STDIN_FILENO, TCSANOW, &no_echo_t);
            }

            char passbuf[256];
            passbuf[0] = '\0';
            if (fgets(passbuf, sizeof(passbuf), stdin) != NULL) {
                size_t len = strlen(passbuf);
                if (len > 0 && passbuf[len - 1] == '\n')
                    passbuf[len - 1] = '\0';
            }
            if (is_tty) {
                tcsetattr(STDIN_FILENO, TCSANOW, &orig_t);
                fputc('\n', stderr);
            }
            r[i].resp = strdup(passbuf);
            explicit_bzero(passbuf, sizeof(passbuf));
        } else if (m->msg_style == PAM_PROMPT_ECHO_ON) {
            if (m->msg) {
                fputs(m->msg, stderr);
                fflush(stderr);
            }
            char textbuf[256];
            textbuf[0] = '\0';
            if (fgets(textbuf, sizeof(textbuf), stdin) != NULL) {
                size_t len = strlen(textbuf);
                if (len > 0 && textbuf[len - 1] == '\n')
                    textbuf[len - 1] = '\0';
            }
            r[i].resp = strdup(textbuf);
        } else if (m->msg_style == PAM_ERROR_MSG || m->msg_style == PAM_TEXT_INFO) {
            if (m->msg) {
                fputs(m->msg, stderr);
                fputc('\n', stderr);
                fflush(stderr);
            }
        }
    }

    *resp = r;
    return PAM_SUCCESS;
}

int pam_start(const char *service, const char *user,
              const struct pam_conv *pam_conv, pam_handle_t **pamh) {
    if (!service || !pamh)
        return PAM_SYSTEM_ERR;

    pam_handle_t *h = calloc(1, sizeof(pam_handle_t));
    if (!h)
        return PAM_BUF_ERR;

    h->service = strdup(service);
    if (user)
        h->user = strdup(user);
    if (pam_conv)
        h->conv = *pam_conv;

    *pamh = h;
    return PAM_SUCCESS;
}

int pam_end(pam_handle_t *pamh, int pam_status) {
    if (!pamh)
        return PAM_SUCCESS;

    struct pam_data_entry *d = pamh->data;
    while (d) {
        struct pam_data_entry *next = d->next;
        if (d->cleanup)
            d->cleanup(pamh, d->data, pam_status);
        free(d->name);
        free(d);
        d = next;
    }

    free(pamh->service);
    free(pamh->user);
    free(pamh->tty);
    free(pamh->rhost);
    free(pamh->ruser);
    if (pamh->authtok) {
        explicit_bzero(pamh->authtok, strlen(pamh->authtok));
        free(pamh->authtok);
    }
    if (pamh->oldauthtok) {
        explicit_bzero(pamh->oldauthtok, strlen(pamh->oldauthtok));
        free(pamh->oldauthtok);
    }
    free(pamh);
    return PAM_SUCCESS;
}

int pam_set_item(pam_handle_t *pamh, int item_type, const void *item) {
    if (!pamh)
        return PAM_SYSTEM_ERR;

    switch (item_type) {
    case PAM_SERVICE:
        free(pamh->service);
        pamh->service = item ? strdup((const char *)item) : NULL;
        break;
    case PAM_USER:
        free(pamh->user);
        pamh->user = item ? strdup((const char *)item) : NULL;
        break;
    case PAM_TTY:
        free(pamh->tty);
        pamh->tty = item ? strdup((const char *)item) : NULL;
        break;
    case PAM_RHOST:
        free(pamh->rhost);
        pamh->rhost = item ? strdup((const char *)item) : NULL;
        break;
    case PAM_RUSER:
        free(pamh->ruser);
        pamh->ruser = item ? strdup((const char *)item) : NULL;
        break;
    case PAM_AUTHTOK:
        if (pamh->authtok) {
            explicit_bzero(pamh->authtok, strlen(pamh->authtok));
            free(pamh->authtok);
        }
        pamh->authtok = item ? strdup((const char *)item) : NULL;
        break;
    case PAM_OLDAUTHTOK:
        if (pamh->oldauthtok) {
            explicit_bzero(pamh->oldauthtok, strlen(pamh->oldauthtok));
            free(pamh->oldauthtok);
        }
        pamh->oldauthtok = item ? strdup((const char *)item) : NULL;
        break;
    case PAM_CONV:
        if (item)
            pamh->conv = *(const struct pam_conv *)item;
        break;
    default:
        return PAM_SYMBOL_ERR;
    }
    return PAM_SUCCESS;
}

int pam_get_item(const pam_handle_t *pamh, int item_type, const void **item) {
    if (!pamh || !item)
        return PAM_SYSTEM_ERR;

    switch (item_type) {
    case PAM_SERVICE:
        *item = pamh->service;
        break;
    case PAM_USER:
        *item = pamh->user;
        break;
    case PAM_TTY:
        *item = pamh->tty;
        break;
    case PAM_RHOST:
        *item = pamh->rhost;
        break;
    case PAM_RUSER:
        *item = pamh->ruser;
        break;
    case PAM_AUTHTOK:
        *item = pamh->authtok;
        break;
    case PAM_OLDAUTHTOK:
        *item = pamh->oldauthtok;
        break;
    case PAM_CONV:
        *item = &pamh->conv;
        break;
    default:
        return PAM_SYMBOL_ERR;
    }
    return PAM_SUCCESS;
}

int pam_set_data(pam_handle_t *pamh, const char *module_data_name, void *data,
                 void (*cleanup)(pam_handle_t *pamh, void *data, int pam_end_status)) {
    if (!pamh || !module_data_name)
        return PAM_SYSTEM_ERR;

    struct pam_data_entry *d = pamh->data;
    while (d) {
        if (strcmp(d->name, module_data_name) == 0) {
            if (d->cleanup)
                d->cleanup(pamh, d->data, PAM_SUCCESS);
            d->data = data;
            d->cleanup = cleanup;
            return PAM_SUCCESS;
        }
        d = d->next;
    }

    struct pam_data_entry *entry = malloc(sizeof(struct pam_data_entry));
    if (!entry)
        return PAM_BUF_ERR;

    entry->name = strdup(module_data_name);
    entry->data = data;
    entry->cleanup = cleanup;
    entry->next = pamh->data;
    pamh->data = entry;
    return PAM_SUCCESS;
}

int pam_get_data(const pam_handle_t *pamh, const char *module_data_name, const void **data) {
    if (!pamh || !module_data_name || !data)
        return PAM_SYSTEM_ERR;

    struct pam_data_entry *d = pamh->data;
    while (d) {
        if (strcmp(d->name, module_data_name) == 0) {
            *data = d->data;
            return PAM_SUCCESS;
        }
        d = d->next;
    }
    return PAM_NO_MODULE_DATA;
}

int pam_authenticate(pam_handle_t *pamh, int flags) {
    (void)flags;
    if (!pamh || !pamh->user)
        return PAM_USER_UNKNOWN;

    struct passwd *pw = getpwnam(pamh->user);
    if (!pw)
        return PAM_USER_UNKNOWN;

    /* If account has empty password */
    if (!pw->pw_passwd || pw->pw_passwd[0] == '\0')
        return PAM_SUCCESS;

    /* If locked account */
    if (pw->pw_passwd[0] == '*')
        return PAM_AUTH_ERR;

    /* Prompt for password via conversation function */
    if (!pamh->conv.conv)
        return PAM_CONV_ERR;

    struct pam_message msg = {
        .msg_style = PAM_PROMPT_ECHO_OFF,
        .msg = "Password: "
    };
    const struct pam_message *msgp = &msg;
    struct pam_response *resp = NULL;

    int r = pamh->conv.conv(1, &msgp, &resp, pamh->conv.appdata_ptr);
    if (r != PAM_SUCCESS || !resp || !resp[0].resp) {
        if (resp) {
            free(resp[0].resp);
            free(resp);
        }
        return PAM_AUTH_ERR;
    }

    char *entered_pw = resp[0].resp;
    char *hash = crypt(entered_pw, pw->pw_passwd);
    int ok = (hash && strcmp(hash, pw->pw_passwd) == 0);

    explicit_bzero(entered_pw, strlen(entered_pw));
    free(entered_pw);
    free(resp);

    return ok ? PAM_SUCCESS : PAM_AUTH_ERR;
}

int pam_setcred(pam_handle_t *pamh, int flags) {
    (void)pamh; (void)flags;
    return PAM_SUCCESS;
}

int pam_acct_mgmt(pam_handle_t *pamh, int flags) {
    (void)pamh; (void)flags;
    return PAM_SUCCESS;
}

int pam_open_session(pam_handle_t *pamh, int flags) {
    (void)pamh; (void)flags;
    return PAM_SUCCESS;
}

int pam_close_session(pam_handle_t *pamh, int flags) {
    (void)pamh; (void)flags;
    return PAM_SUCCESS;
}

/*
 * pam_chauthtok — changes password in /etc/master.passwd and /etc/passwd
 */
int pam_chauthtok(pam_handle_t *pamh, int flags) {
    (void)flags;
    if (!pamh || !pamh->user || !pamh->conv.conv)
        return PAM_SYSTEM_ERR;

    struct passwd *pw = getpwnam(pamh->user);
    if (!pw)
        return PAM_USER_UNKNOWN;

    uid_t my_uid = getuid();

    /* 1. Prompt for old password if not root */
    if (my_uid != 0 && pw->pw_passwd && pw->pw_passwd[0] != '\0' && pw->pw_passwd[0] != '*') {
        struct pam_message msg = {
            .msg_style = PAM_PROMPT_ECHO_OFF,
            .msg = "Old Password: "
        };
        const struct pam_message *msgp = &msg;
        struct pam_response *resp = NULL;
        int r = pamh->conv.conv(1, &msgp, &resp, pamh->conv.appdata_ptr);
        if (r != PAM_SUCCESS || !resp || !resp[0].resp) {
            if (resp) { free(resp[0].resp); free(resp); }
            return PAM_AUTH_ERR;
        }
        char *hash = crypt(resp[0].resp, pw->pw_passwd);
        int match = (hash && strcmp(hash, pw->pw_passwd) == 0);
        explicit_bzero(resp[0].resp, strlen(resp[0].resp));
        free(resp[0].resp);
        free(resp);
        if (!match)
            return PAM_AUTH_ERR;
    }

    /* 2. Prompt for new password */
    struct pam_message msg1 = {
        .msg_style = PAM_PROMPT_ECHO_OFF,
        .msg = "New Password: "
    };
    const struct pam_message *msgp1 = &msg1;
    struct pam_response *resp1 = NULL;
    int r = pamh->conv.conv(1, &msgp1, &resp1, pamh->conv.appdata_ptr);
    if (r != PAM_SUCCESS || !resp1 || !resp1[0].resp) {
        if (resp1) { free(resp1[0].resp); free(resp1); }
        return PAM_AUTHTOK_ERR;
    }
    char *new_pass = resp1[0].resp;

    /* 3. Retype new password */
    struct pam_message msg2 = {
        .msg_style = PAM_PROMPT_ECHO_OFF,
        .msg = "Retype New Password: "
    };
    const struct pam_message *msgp2 = &msg2;
    struct pam_response *resp2 = NULL;
    r = pamh->conv.conv(1, &msgp2, &resp2, pamh->conv.appdata_ptr);
    if (r != PAM_SUCCESS || !resp2 || !resp2[0].resp) {
        explicit_bzero(new_pass, strlen(new_pass));
        free(new_pass);
        free(resp1);
        if (resp2) { free(resp2[0].resp); free(resp2); }
        return PAM_AUTHTOK_ERR;
    }
    char *confirm_pass = resp2[0].resp;

    if (strcmp(new_pass, confirm_pass) != 0) {
        struct pam_message err_msg = {
            .msg_style = PAM_ERROR_MSG,
            .msg = "Mismatch; try again, error: Passwords do not match"
        };
        const struct pam_message *err_p = &err_msg;
        struct pam_response *err_resp = NULL;
        pamh->conv.conv(1, &err_p, &err_resp, pamh->conv.appdata_ptr);
        if (err_resp) { free(err_resp[0].resp); free(err_resp); }
        explicit_bzero(new_pass, strlen(new_pass));
        explicit_bzero(confirm_pass, strlen(confirm_pass));
        free(new_pass);
        free(confirm_pass);
        free(resp1);
        free(resp2);
        return PAM_AUTHTOK_ERR;
    }

    /* 4. Generate SHA-512 salt & hash */
    static const char salt_chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";
    char salt[32];
    salt[0] = '$'; salt[1] = '6'; salt[2] = '$';
    for (int i = 3; i < 19; i++) {
        salt[i] = salt_chars[rand() % (sizeof(salt_chars) - 1)];
    }
    salt[19] = '$';
    salt[20] = '\0';

    char *new_hash = crypt(new_pass, salt);
    if (!new_hash) {
        explicit_bzero(new_pass, strlen(new_pass));
        explicit_bzero(confirm_pass, strlen(confirm_pass));
        free(new_pass);
        free(confirm_pass);
        free(resp1);
        free(resp2);
        return PAM_AUTHTOK_ERR;
    }

    /* 5. Update /etc/master.passwd and /etc/passwd */
    FILE *mf = fopen("/etc/master.passwd", "r");
    if (!mf) {
        explicit_bzero(new_pass, strlen(new_pass));
        explicit_bzero(confirm_pass, strlen(confirm_pass));
        free(new_pass);
        free(confirm_pass);
        free(resp1);
        free(resp2);
        return PAM_SYSTEM_ERR;
    }

    char tmp_master[16384] = {0};
    char tmp_passwd[16384] = {0};
    char line[1024];

    while (fgets(line, sizeof(line), mf) != NULL) {
        char lcopy[1024];
        strncpy(lcopy, line, sizeof(lcopy) - 1);
        char *colon = strchr(lcopy, ':');
        if (colon) {
            *colon = '\0';
            if (strcmp(lcopy, pw->pw_name) == 0) {
                /* Replace password field in master.passwd */
                char entry[1024];
                snprintf(entry, sizeof(entry), "%s:%s:%u:%u:%s:%ld:%ld:%s:%s:%s\n",
                         pw->pw_name, new_hash,
                         (unsigned)pw->pw_uid, (unsigned)pw->pw_gid,
                         pw->pw_class ? pw->pw_class : "",
                         (long)pw->pw_change, (long)pw->pw_expire,
                         pw->pw_gecos ? pw->pw_gecos : "",
                         pw->pw_dir ? pw->pw_dir : "/",
                         pw->pw_shell ? pw->pw_shell : "/bin/zsh");
                strncat(tmp_master, entry, sizeof(tmp_master) - strlen(tmp_master) - 1);

                /* Standard /etc/passwd without hash */
                char pentry[1024];
                snprintf(pentry, sizeof(pentry), "%s:*:%u:%u:%s:%s:%s\n",
                         pw->pw_name,
                         (unsigned)pw->pw_uid, (unsigned)pw->pw_gid,
                         pw->pw_gecos ? pw->pw_gecos : "",
                         pw->pw_dir ? pw->pw_dir : "/",
                         pw->pw_shell ? pw->pw_shell : "/bin/zsh");
                strncat(tmp_passwd, pentry, sizeof(tmp_passwd) - strlen(tmp_passwd) - 1);
                continue;
            }
        }
        strncat(tmp_master, line, sizeof(tmp_master) - strlen(tmp_master) - 1);
    }
    fclose(mf);

    /* Write updated /etc/master.passwd */
    FILE *outm = fopen("/etc/master.passwd", "w");
    if (outm) {
        fputs(tmp_master, outm);
        fclose(outm);
    }

    /* Also write updated /etc/passwd */
    if (tmp_passwd[0] != '\0') {
        FILE *outp = fopen("/etc/passwd", "w");
        if (outp) {
            fputs(tmp_passwd, outp);
            fclose(outp);
        }
    }

    explicit_bzero(new_pass, strlen(new_pass));
    explicit_bzero(confirm_pass, strlen(confirm_pass));
    free(new_pass);
    free(confirm_pass);
    free(resp1);
    free(resp2);

    return PAM_SUCCESS;
}

const char *pam_strerror(const pam_handle_t *pamh, int errnum) {
    (void)pamh;
    switch (errnum) {
    case PAM_SUCCESS: return "Success";
    case PAM_OPEN_ERR: return "Failed to load module";
    case PAM_SYMBOL_ERR: return "Symbol not found";
    case PAM_SERVICE_ERR: return "Error in service module";
    case PAM_SYSTEM_ERR: return "System error";
    case PAM_BUF_ERR: return "Memory buffer error";
    case PAM_PERM_DENIED: return "Permission denied";
    case PAM_AUTH_ERR: return "Authentication error";
    case PAM_AUTHINFO_UNAVAIL: return "Authentication information unavailable";
    case PAM_USER_UNKNOWN: return "User unknown";
    case PAM_MAXTRIES: return "Maximum number of tries exceeded";
    case PAM_AUTHTOK_ERR: return "Authentication token error";
    case PAM_AUTHTOK_RECOVERY_ERR: return "Authentication token recovery error";
    case PAM_AUTHTOK_LOCK_BUSY: return "Authentication token lock busy";
    case PAM_AUTHTOK_DISABLE_AGING: return "Authentication token aging disabled";
    case PAM_NO_MODULE_DATA: return "No module data";
    case PAM_IGNORE: return "Ignore module";
    case PAM_ABORT: return "Transaction aborted";
    case PAM_TRY_AGAIN: return "Try again";
    default: return "Unknown PAM error";
    }
}
