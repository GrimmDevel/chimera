/* =============================================================================
 * XIU Userland SDK — <netdb.h>
 * ============================================================================= */

#ifndef _NETDB_H_
#define _NETDB_H_

#include <netinet/in.h>

struct hostent {
    char  *h_name;
    char **h_aliases;
    int    h_addrtype;
    int    h_length;
    char **h_addr_list;
};
#define h_addr h_addr_list[0]

#ifdef __cplusplus
extern "C" {
#endif

struct hostent *gethostbyname(const char *name);
struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type);

#ifdef __cplusplus
}
#endif

#endif /* _NETDB_H_ */
