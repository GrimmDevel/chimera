/*
 * XIU Operating System — OpenPAM Extensions Header
 */

#ifndef _SECURITY_OPENPAM_H_
#define _SECURITY_OPENPAM_H_

#include <security/pam_appl.h>

__BEGIN_DECLS

int openpam_ttyconv(int num_msg, const struct pam_message **msg,
                    struct pam_response **resp, void *data);

int openpam_nullconv(int num_msg, const struct pam_message **msg,
                     struct pam_response **resp, void *data);

__END_DECLS

#endif /* !_SECURITY_OPENPAM_H_ */
