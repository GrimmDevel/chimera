/* =============================================================================
 * XIU Userland SDK — <sys/socket.h>
 * ============================================================================= */

#ifndef _SYS_SOCKET_H_
#define _SYS_SOCKET_H_

#include <stdint.h>
#include <sys/types.h>

#define AF_UNSPEC       0
#define AF_UNIX         1
#define AF_LOCAL        AF_UNIX
#define AF_INET         2
#define AF_INET6        30

#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define SOCK_RAW        3

#define SOL_SOCKET      0xffff
#define SO_DEBUG        0x0001
#define SO_REUSEADDR    0x0004
#define SO_KEEPALIVE    0x0008
#define SO_DONTROUTE    0x0010
#define SO_BROADCAST    0x0020

#define SHUT_RD         0
#define SHUT_WR         1
#define SHUT_RDWR       2

typedef uint8_t   sa_family_t;
typedef uint32_t  socklen_t;

struct sockaddr {
    uint8_t       sa_len;
    sa_family_t   sa_family;
    char          sa_data[14];
};

#ifdef __cplusplus
extern "C" {
#endif

int socket(int domain, int type, int protocol);
int bind(int socket, const struct sockaddr *address, socklen_t address_len);
int connect(int socket, const struct sockaddr *address, socklen_t address_len);
int listen(int socket, int backlog);
int accept(int socket, struct sockaddr *address, socklen_t *address_len);
ssize_t send(int socket, const void *buffer, size_t length, int flags);
ssize_t recv(int socket, void *buffer, size_t length, int flags);
ssize_t sendto(int socket, const void *buffer, size_t length, int flags,
               const struct sockaddr *dest_addr, socklen_t dest_len);
ssize_t recvfrom(int socket, void *buffer, size_t length, int flags,
                 struct sockaddr *address, socklen_t *address_len);
int shutdown(int socket, int how);
int setsockopt(int socket, int level, int option_name, const void *option_value, socklen_t option_len);
int getsockopt(int socket, int level, int option_name, void *option_value, socklen_t *option_len);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SOCKET_H_ */
