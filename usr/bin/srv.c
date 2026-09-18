/* =============================================================================
 * XIU Operating System — Minimal TCP Echo Server (srv)
 * usr/bin/srv.c
 *
 * usage: srv <port>
 * Accepts one connection, echoes every received line back to the peer and
 * to stdout, exits when the peer closes. Test tool for TCP listen/accept.
 * ============================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: srv <port>\n");
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        printf("srv: invalid port: %s\n", argv[1]);
        return 1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("srv: socket failed\n");
        return 1;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    sin.sin_addr.s_addr = 0; // INADDR_ANY

    if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        printf("srv: bind failed\n");
        close(fd);
        return 1;
    }

    if (listen(fd, 1) != 0) {
        printf("srv: listen failed\n");
        close(fd);
        return 1;
    }

    printf("srv: listening on port %d\n", port);

    int c = accept(fd, NULL, NULL);
    if (c < 0) {
        printf("srv: accept failed\n");
        close(fd);
        return 1;
    }
    printf("srv: connection accepted\n");

    char buf[512];
    for (;;) {
        long n = read(c, buf, sizeof(buf));
        if (n <= 0)
            break;
        write(1, buf, (size_t)n);      // print to console
        write(c, buf, (size_t)n);      // echo back
    }

    printf("srv: connection closed\n");
    close(c);
    close(fd);
    return 0;
}
