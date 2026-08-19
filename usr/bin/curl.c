/* =============================================================================
 * XIU Operating System — Lightweight HTTP Client Utility (curl)
 * usr/bin/curl.c
 * ============================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BUFFER_SIZE 4096

typedef struct {
    char protocol[16];
    char host[128];
    int  port;
    char path[256];
} url_parts_t;

static int parse_url(const char *url, url_parts_t *parts) {
    memset(parts, 0, sizeof(*parts));
    strcpy(parts->path, "/");

    const char *p = url;
    const char *proto_end = strstr(p, "://");
    if (proto_end) {
        size_t proto_len = proto_end - p;
        if (proto_len < sizeof(parts->protocol)) {
            memcpy(parts->protocol, p, proto_len);
            parts->protocol[proto_len] = '\0';
        }
        p = proto_end + 3;
    } else {
        strcpy(parts->protocol, "http");
    }

    if (strcmp(parts->protocol, "https") == 0) {
        parts->port = 443;
    } else {
        parts->port = 80;
    }

    // check for host and optional port
    const char *path_start = strchr(p, '/');
    const char *port_start = strchr(p, ':');

    if (port_start && (!path_start || port_start < path_start)) {
        size_t host_len = port_start - p;
        if (host_len >= sizeof(parts->host)) host_len = sizeof(parts->host) - 1;
        memcpy(parts->host, p, host_len);
        parts->host[host_len] = '\0';

        parts->port = atoi(port_start + 1);
    } else {
        size_t host_len = path_start ? (size_t)(path_start - p) : strlen(p);
        if (host_len >= sizeof(parts->host)) host_len = sizeof(parts->host) - 1;
        memcpy(parts->host, p, host_len);
        parts->host[host_len] = '\0';
    }

    if (path_start) {
        strncpy(parts->path, path_start, sizeof(parts->path) - 1);
    }

    return (parts->host[0] != '\0') ? 0 : -1;
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options...] <url>\n", prog);
    printf("Options:\n");
    printf("  -v, --verbose        Make the operation more talkative\n");
    printf("  -i, --include        Include HTTP-headers in the output\n");
    printf("  -I, --head           Show document info only\n");
    printf("  -o, --output <file>  Write to file instead of stdout\n");
    printf("  -X, --request <cmd>  Specify request command (GET, POST, HEAD)\n");
    printf("  -d, --data <data>    HTTP POST data\n");
    printf("  -H, --header <line>  Extra header to include in information sent\n");
    printf("  -h, --help           This help text\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *url = NULL;
    const char *output_file = NULL;
    const char *method = "GET";
    const char *post_data = NULL;
    const char *custom_header = NULL;
    int verbose = 0;
    int include_headers = 0;
    int head_only = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--include") == 0) {
            include_headers = 1;
        } else if (strcmp(argv[i], "-I") == 0 || strcmp(argv[i], "--head") == 0) {
            head_only = 1;
            method = "HEAD";
        } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            output_file = argv[++i];
        } else if ((strcmp(argv[i], "-X") == 0 || strcmp(argv[i], "--request") == 0) && i + 1 < argc) {
            method = argv[++i];
        } else if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--data") == 0) && i + 1 < argc) {
            post_data = argv[++i];
            method = "POST";
        } else if ((strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "--header") == 0) && i + 1 < argc) {
            custom_header = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            url = argv[i];
        }
    }

    if (!url) {
        printf("curl: no URL specified!\n");
        return 1;
    }

    url_parts_t parts;
    if (parse_url(url, &parts) != 0) {
        printf("curl: failed to parse URL: %s\n", url);
        return 1;
    }

    in_addr_t server_ip = 0;
    struct hostent *he = gethostbyname(parts.host);
    if (he && he->h_addr_list && he->h_addr_list[0]) {
        server_ip = *(in_addr_t *)he->h_addr_list[0];
    } else {
        server_ip = inet_addr(parts.host);
    }

    if (server_ip == (in_addr_t)-1 || server_ip == 0) {
        printf("curl: could not resolve host: %s\n", parts.host);
        return 1;
    }

    if (verbose) {
        struct in_addr in;
        in.s_addr = server_ip;
        printf("* Host %s was resolved to %s.\n", parts.host, inet_ntoa(in));
        printf("* Trying %s:%d...\n", inet_ntoa(in), parts.port);
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        printf("curl: failed to create socket\n");
        return 1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((uint16_t)parts.port);
    serv_addr.sin_addr.s_addr = server_ip;

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
        printf("curl: (7) Failed to connect to %s port %d\n", parts.host, parts.port);
        close(sockfd);
        return 1;
    }

    if (verbose) {
        printf("* Connected to %s (%s) port %d\n", parts.host, parts.host, parts.port);
    }

    // construct HTTP Request
    char req_buf[BUFFER_SIZE];
    int req_len = 0;

    req_len += snprintf(req_buf + req_len, sizeof(req_buf) - req_len,
                        "%s %s HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "User-Agent: curl/8.4.0 (x86_64-apple-darwin; XIU-OS)\r\n"
                        "Accept: */*\r\n",
                        method, parts.path, parts.host);

    if (custom_header) {
        req_len += snprintf(req_buf + req_len, sizeof(req_buf) - req_len,
                            "%s\r\n", custom_header);
    }

    if (post_data) {
        req_len += snprintf(req_buf + req_len, sizeof(req_buf) - req_len,
                            "Content-Type: application/x-www-form-urlencoded\r\n"
                            "Content-Length: %zu\r\n",
                            strlen(post_data));
    }

    req_len += snprintf(req_buf + req_len, sizeof(req_buf) - req_len,
                        "Connection: close\r\n\r\n");

    if (post_data) {
        req_len += snprintf(req_buf + req_len, sizeof(req_buf) - req_len,
                            "%s", post_data);
    }

    if (verbose) {
        printf("> %s %s HTTP/1.1\n", method, parts.path);
        printf("> Host: %s\n", parts.host);
        printf("> User-Agent: curl/8.4.0 (XIU-OS)\n");
        printf("> Accept: */*\n");
        if (custom_header) printf("> %s\n", custom_header);
        printf(">\n");
    }

    // send HTTP Request
    ssize_t sent = send(sockfd, req_buf, req_len, 0);
    if (sent < 0) {
        printf("curl: (55) Failed sending data to peer\n");
        close(sockfd);
        return 1;
    }

    // open output target
    int out_fd = 1; // stdout default
    if (output_file) {
        out_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd < 0) {
            printf("curl: failed to open output file: %s\n", output_file);
            close(sockfd);
            return 1;
        }
    }

    // read Response
    char rx_buf[BUFFER_SIZE];
    ssize_t n = 0;
    int in_headers = 1;
    size_t header_bytes = 0;
    size_t body_bytes = 0;

    while ((n = recv(sockfd, rx_buf, sizeof(rx_buf) - 1, 0)) > 0) {
        rx_buf[n] = '\0';

        if (in_headers) {
            char *hdr_end = strstr(rx_buf, "\r\n\r\n");
            if (!hdr_end) hdr_end = strstr(rx_buf, "\n\n");

            if (hdr_end) {
                size_t hdr_len = 0;
                size_t split_offset = 0;

                if (strstr(rx_buf, "\r\n\r\n")) {
                    hdr_len = (hdr_end - rx_buf) + 2;
                    split_offset = (hdr_end - rx_buf) + 4;
                } else {
                    hdr_len = (hdr_end - rx_buf) + 1;
                    split_offset = (hdr_end - rx_buf) + 2;
                }

                header_bytes += hdr_len;

                if (verbose) {
                    // print headers prefixed with <
                    char hdr_tmp[BUFFER_SIZE];
                    memcpy(hdr_tmp, rx_buf, hdr_len);
                    hdr_tmp[hdr_len] = '\0';
                    char *line = strtok(hdr_tmp, "\r\n");
                    while (line) {
                        printf("< %s\n", line);
                        line = strtok(NULL, "\r\n");
                    }
                    printf("<\n");
                }

                if (include_headers || head_only) {
                    write(out_fd, rx_buf, split_offset);
                }

                if (!head_only && split_offset < (size_t)n) {
                    size_t body_chunk = n - split_offset;
                    write(out_fd, rx_buf + split_offset, body_chunk);
                    body_bytes += body_chunk;
                }

                in_headers = 0;
                if (head_only) break;
            } else {
                header_bytes += n;
                if (include_headers || head_only) {
                    write(out_fd, rx_buf, n);
                }
            }
        } else {
            write(out_fd, rx_buf, n);
            body_bytes += n;
        }
    }

    if (output_file && out_fd > 2) {
        close(out_fd);
    }

    close(sockfd);

    if (verbose) {
        printf("* Closing connection\n");
        printf("* Transferred %zu header bytes, %zu body bytes\n", header_bytes, body_bytes);
    }

    return 0;
}
