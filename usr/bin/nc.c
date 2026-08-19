/* =============================================================================
 * XIU Operating System — Netcat Utility (nc)
 * usr/bin/nc.c
 * ============================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: nc <host_ip> <port>\n");
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);

    if (port <= 0 || port > 65535) {
        printf("nc: invalid port: %s\n", argv[2]);
        return 1;
    }

    in_addr_t addr = inet_addr(host);
    if (addr == (in_addr_t)-1 && strcmp(host, "255.255.255.255") != 0) {
        printf("nc: cannot resolve %s\n", host);
        return 1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        printf("nc: socket creation failed\n");
        return 1;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    sin.sin_addr.s_addr = addr;

    printf("Connecting to %s:%d...\n", host, port);
    if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        printf("nc: connection failed\n");
        close(fd);
        return 1;
    }

    printf("Connected to %s:%d (TCP)\n", host, port);

    // send simple HTTP GET request if port 80
    if (port == 80) {
        const char *req = "GET / HTTP/1.0\r\nHost: xiu-system\r\n\r\n";
        send(fd, req, strlen(req), 0);
    }

    char buf[512];
    ssize_t n = 0;
    while ((n = recv(fd, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        write(1, buf, n);
    }

    close(fd);
    return 0;
}
