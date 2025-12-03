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
#include <sys/stat.h>
#include <fcntl.h>

static ssize_t readn(int fd, void *buf, size_t count) {
    size_t left = count;
    char *p = buf;
    while (left > 0) {
        ssize_t n = read(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
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

static int send_file(int sockfd, const char *path) {
    const char *name = strrchr(path, '/');
    name = (name == NULL) ? path : name + 1;

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return -1;
    }
    uint64_t size = (uint64_t)st.st_size;

    printf("Sending file '%s' (%llu bytes)\n",
           name, (unsigned long long)size);

    if (writen(sockfd, "SEND_FILE\n", 10) != 10) {
        fprintf(stderr, "Failed to send command\n");
        close(fd);
        return -1;
    }

    uint32_t len = (uint32_t)strlen(name);
    uint32_t len_n = htonl(len);
    if (writen(sockfd, &len_n, sizeof(len_n)) != sizeof(len_n)) {
        fprintf(stderr, "Failed to send name length\n");
        close(fd);
        return -1;
    }

    if (writen(sockfd, name, len) != (ssize_t)len) {
        fprintf(stderr, "Failed to send file name\n");
        close(fd);
        return -1;
    }

    uint64_t size_n = htonll(size);
    if (writen(sockfd, &size_n, sizeof(size_n)) != sizeof(size_n)) {
        fprintf(stderr, "Failed to send file size\n");
        close(fd);
        return -1;
    }

    char buffer[4096];
    ssize_t n;
    while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
        if (writen(sockfd, buffer, n) != n) {
            fprintf(stderr, "Failed to send file data\n");
            close(fd);
            return -1;
        }
    }
    if (n < 0) {
        perror("read");
        close(fd);
        return -1;
    }

    printf("File sent.\n");
    close(fd);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr,
                "Usage: %s <server_ip> <port> <file_path>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    const char *path = argv[3];

    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port number.\n");
        return EXIT_FAILURE;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        return EXIT_FAILURE;
    }

    if (connect(sockfd,
                (struct sockaddr *)&serv_addr,
                sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return EXIT_FAILURE;
    }

    if (send_file(sockfd, path) != 0) {
        fprintf(stderr, "File transfer failed.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    close(sockfd);
    return EXIT_SUCCESS;
}
