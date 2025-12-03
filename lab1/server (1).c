#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

static ssize_t readn(int fd, void *buf, size_t count) {
    size_t left = count;
    char *p = buf;
    while (left > 0) {
        ssize_t n = read(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {              // EOF
            return (ssize_t)(count - left);
        }
        left -= n;
        p += n;
    }
    return (ssize_t)count;
}

static ssize_t writen(int fd, const void *buf, size_t count) {
    size_t left = count;
    const char *p = buf;
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        left -= n;
        p += n;
    }
    return (ssize_t)count;
}

static uint64_t htonll(uint64_t host) {
    uint32_t hi = htonl((uint32_t)(host >> 32));
    uint32_t lo = htonl((uint32_t)(host & 0xFFFFFFFFULL));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t ntohll(uint64_t net) {
    uint32_t hi = ntohl((uint32_t)(net >> 32));
    uint32_t lo = ntohl((uint32_t)(net & 0xFFFFFFFFULL));
    return ((uint64_t)hi << 32) | lo;
}

static void handle_client(int connfd) {
    char command[16] = {0};
    if (readn(connfd, command, 10) != 10) {
        fprintf(stderr, "Failed to read command\n");
        return;
    }
    if (strncmp(command, "SEND_FILE\n", 10) != 0) {
        fprintf(stderr, "Unknown command: %.*s\n", 10, command);
        return;
    }

    uint32_t name_len_n;
    if (readn(connfd, &name_len_n, sizeof(name_len_n)) != sizeof(name_len_n)) {
        fprintf(stderr, "Failed to read name length\n");
        return;
    }
    uint32_t name_len = ntohl(name_len_n);
    if (name_len == 0 || name_len > 4096) {
        fprintf(stderr, "Unreasonable file name length: %u\n", name_len);
        return;
    }

    char *filename = malloc(name_len + 1);
    if (!filename) {
        perror("malloc");
        return;
    }
    if (readn(connfd, filename, name_len) != (ssize_t)name_len) {
        fprintf(stderr, "Failed to read file name\n");
        free(filename);
        return;
    }
    filename[name_len] = '\0';

    uint64_t size_n;
    if (readn(connfd, &size_n, sizeof(size_n)) != sizeof(size_n)) {
        fprintf(stderr, "Failed to read file size\n");
        free(filename);
        return;
    }
    uint64_t file_size = ntohll(size_n);

    printf("Receiving file '%s' (%llu bytes)\n",
           filename, (unsigned long long)file_size);

    FILE *out = fopen(filename, "wb");
    if (!out) {
        perror("fopen");
        free(filename);
        return;
    }

    char buffer[4096];
    uint64_t remaining = file_size;
    while (remaining > 0) {
        size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        ssize_t n = read(connfd, buffer, chunk);
        if (n < 0) {
            perror("read");
            break;
        }
        if (n == 0) {
            fprintf(stderr, "Connection closed early\n");
            break;
        }
        fwrite(buffer, 1, n, out);
        remaining -= (uint64_t)n;
    }

    if (remaining == 0) {
        printf("File '%s' received successfully.\n", filename);
    } else {
        fprintf(stderr, "File '%s' incomplete (missing %llu bytes)\n",
                filename, (unsigned long long)remaining);
    }

    fclose(out);
    free(filename);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port number.\n");
        return EXIT_FAILURE;
    }

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int opt = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(listenfd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(listenfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listenfd);
        return EXIT_FAILURE;
    }

    if (listen(listenfd, 5) < 0) {
        perror("listen");
        close(listenfd);
        return EXIT_FAILURE;
    }

    printf("Server listening on port %d...\n", port);

    for (;;) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int connfd = accept(listenfd, (struct sockaddr *)&cli_addr, &cli_len);
        if (connfd < 0) {
            perror("accept");
            continue;
        }

        printf("New connection accepted.\n");
        handle_client(connfd);
        close(connfd);
        printf("Connection closed.\n");
    }

    close(listenfd);
    return EXIT_SUCCESS;
}
