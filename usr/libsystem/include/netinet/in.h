/* =============================================================================
 * XIU Userland SDK — <netinet/in.h>
 * ============================================================================= */

#ifndef _NETINET_IN_H_
#define _NETINET_IN_H_

#include <stdint.h>
#include <sys/socket.h>

#define IPPROTO_IP      0
#define IPPROTO_ICMP    1
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17
#define IPPROTO_RAW     255

#define INADDR_ANY          ((uint32_t)0x00000000)
#define INADDR_LOOPBACK     ((uint32_t)0x7F000001)
#define INADDR_BROADCAST    ((uint32_t)0xFFFFFFFF)
#define INADDR_NONE         ((uint32_t)0xFFFFFFFF)

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

struct in_addr {
    in_addr_t       s_addr;
};

struct sockaddr_in {
    uint8_t         sin_len;
    sa_family_t     sin_family;
    in_port_t       sin_port;
    struct in_addr  sin_addr;
    char            sin_zero[8];
};

struct in6_addr {
    union {
        uint8_t   __u6_addr8[16];
        uint16_t  __u6_addr16[8];
        uint32_t  __u6_addr32[4];
    } __u6_addr;
};
#define s6_addr   __u6_addr.__u6_addr8
#define s6_addr16 __u6_addr.__u6_addr16
#define s6_addr32 __u6_addr.__u6_addr32

struct sockaddr_in6 {
    uint8_t         sin6_len;
    sa_family_t     sin6_family;
    in_port_t       sin6_port;
    uint32_t        sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t        sin6_scope_id;
};

#endif /* _NETINET_IN_H_ */
