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

#endif /* _NETINET_IN_H_ */
