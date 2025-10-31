#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/sha.h>

#define BLOCK_SIZE 100

ssize_t send_all(int s, const void *buf, size_t len) {
    const char *p = buf;
    while (len > 0) {
        ssize_t n = send(s, p, len, 0);
        if (n <= 0) return n;
        p += n; len -= n;
    }
    return 1;
}

ssize_t recv_all(int s, void *buf, size_t len) {
    char *p = buf;
    while (len > 0) {
        ssize_t n = recv(s, p, len, 0);
        if (n <= 0) return n;
        p += n; len -= n;
    }
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Uso: %s <ip> <puerto>\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, ip, &sa.sin_addr);

    if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        perror("connect");
        return 1;
    }

    while (1) {
        // Solicitar trabajo
        char R = 'R';
        if (send_all(s, &R, 1) <= 0) break;

        uint32_t type;
        if (recv_all(s, &type, sizeof(type)) <= 0) break;
        type = ntohl(type);

        if (type == 0) {
            printf("Worker: no hay más trabajos, desconectando...\n");
            break;
        }
        if (type != 1) continue;

        uint32_t jid, len;
        recv_all(s, &jid, sizeof(jid)); jid = ntohl(jid);
        recv_all(s, &len, sizeof(len)); len = ntohl(len);
        char buf[BLOCK_SIZE];
        recv_all(s, buf, len);
        uint8_t diff;
        recv_all(s, &diff, 1);
        uint64_t start_net, range_net;
        recv_all(s, &start_net, sizeof(start_net));
        recv_all(s, &range_net, sizeof(range_net));
        uint64_t start = be64toh(start_net);
        uint64_t range = be64toh(range_net);

        unsigned char hash[SHA256_DIGEST_LENGTH];
        uint64_t found_nonce = 0;
        int found = 0;

        for (uint64_t nonce = start; nonce < start + range; nonce++) {
            unsigned char data[BLOCK_SIZE + 8];
            memcpy(data, buf, len);
            uint64_t n = htobe64(nonce);
            memcpy(data + len, &n, 8);
            SHA256(data, len + 8, hash);

            int zeros = 0;
            for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
                if (hash[i] == 0x00) zeros += 2;
                else if ((hash[i] >> 4) == 0) { zeros++; break; }
                else break;
            }

            if (zeros >= diff) {
                found = 1;
                found_nonce = nonce;
                break;
            }
        }

        uint32_t rtype = htonl(2);
        send_all(s, &rtype, sizeof(rtype));
        uint32_t jid_net = htonl(jid);
        send_all(s, &jid_net, sizeof(jid_net));
        uint8_t fbyte = found ? 1 : 0;
        send_all(s, &fbyte, 1);
        if (found) {
            uint64_t n_net = htobe64(found_nonce);
            send_all(s, &n_net, sizeof(n_net));
            send_all(s, hash, SHA256_DIGEST_LENGTH);
            printf("Worker encontró hash (job %u) nonce=%lu\n", jid, found_nonce);
        }
    }

    close(s);
    return 0;
}

