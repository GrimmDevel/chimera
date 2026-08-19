/* =============================================================================
 * XIU Userland SDK — <arpa/inet.h>
 * ============================================================================= */

#ifndef _ARPA_INET_H_
#define _ARPA_INET_H_

#include <stdint.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

in_addr_t       inet_addr(const char *cp);
char           *inet_ntoa(struct in_addr in);
int             inet_pton(int af, const char *src, void *dst);
const char     *inet_ntop(int af, const void *src, char *dst, socklen_t size);

uint16_t        htons(uint16_t hostshort);
uint16_t        ntohs(uint16_t netshort);
uint32_t        htonl(uint32_t hostlong);
uint32_t        ntohl(uint32_t netlong);

#ifdef __cplusplus
}
#endif

#endif /* _ARPA_INET_H_ */
