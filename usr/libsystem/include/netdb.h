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

#ifndef _STRUCT_ADDRINFO
#define _STRUCT_ADDRINFO
#define HAVE_GETADDRINFO 1
#define HAVE_GAI_STRERROR 1
#define COMPAT_GETADDRINFO_H 1

struct addrinfo {
    int              ai_flags;
    int              ai_family;
    int              ai_socktype;
    int              ai_protocol;
    socklen_t        ai_addrlen;
    char            *ai_canonname;
    struct sockaddr *ai_addr;
    struct addrinfo *ai_next;
};
#endif


#define AI_PASSIVE     0x00000001
#define AI_CANONNAME   0x00000002
#define AI_NUMERICHOST 0x00000004
#define AI_NUMERICSERV 0x00000008

#define NI_NUMERICHOST 0x00000001
#define NI_NUMERICSERV 0x00000002
#define NI_NOFQDN      0x00000004
#define NI_NAMEREQD    0x00000008
#define NI_DGRAM       0x00000010

#define NI_MAXHOST 1025
#define NI_MAXSERV 32

#ifdef __cplusplus
extern "C" {
#endif

struct hostent *gethostbyname(const char *name);
struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type);
int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);
const char *gai_strerror(int errcode);
int getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen, char *serv, socklen_t servlen, int flags);

#ifdef __cplusplus
}
#endif

#endif /* _NETDB_H_ */

