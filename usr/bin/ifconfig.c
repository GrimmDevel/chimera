/* =============================================================================
 * XIU Operating System — Network Interface Config Utility (ifconfig)
 * usr/bin/ifconfig.c
 * ============================================================================= */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("lo0: flags=8049<UP,LOOPBACK,RUNNING,MULTICAST> mtu 16384\n");
    printf("\tinet 127.0.0.1 netmask 0xff000000\n");
    printf("\tstatus: active\n\n");

    printf("en0: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500\n");
    printf("\tether 52:54:00:12:34:56\n");
    printf("\tinet 10.0.2.15 netmask 0xffffff00 broadcast 10.0.2.255\n");
    printf("\tmedia: autoselect (1000baseT <full-duplex>)\n");
    printf("\tstatus: active\n");

    return 0;
}
