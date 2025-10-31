#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <sys/time.h>

#define BLOCK_SIZE 100
#define BACKLOG 5

typedef struct {
    int id;
    size_t len;
    char *data;
} Job;

Job *jobs;
int total_jobs;
int *done;
uint64_t *next_nonce;
int difficulty = 4;
uint64_t range_size = 100000;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

struct timeval start_time, end_time;
struct timeval *job_start_times;

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

void *worker_thread(void *arg) {
    int client = *(int*)arg;
    free(arg);

    while (1) {
        char req;
        if (recv_all(client, &req, 1) <= 0) break;
        if (req != 'R') continue;

        pthread_mutex_lock(&lock);
        int jid = -1;
        for (int i = 0; i < total_jobs; i++) {
            if (!done[i]) { jid = i; break; }
        }


        if (jid == -1) {
            pthread_mutex_unlock(&lock);
            uint32_t t0 = htonl(0);
            send_all(client, &t0, sizeof(t0));
            usleep(50000);
            continue;
        }


        uint64_t start = next_nonce[jid];
        next_nonce[jid] += range_size;


        if (next_nonce[jid] == range_size)
            gettimeofday(&job_start_times[jid], NULL);

        pthread_mutex_unlock(&lock);


        uint32_t t1 = htonl(1);
        send_all(client, &t1, sizeof(t1));
        uint32_t jid_net = htonl(jid);
        uint32_t len_net = htonl(jobs[jid].len);
        send_all(client, &jid_net, sizeof(jid_net));
        send_all(client, &len_net, sizeof(len_net));
        send_all(client, jobs[jid].data, jobs[jid].len);

        uint8_t diff = (uint8_t)difficulty;
        send_all(client, &diff, 1);
        uint64_t s_net = htobe64(start);
        uint64_t r_net = htobe64(range_size);
        send_all(client, &s_net, sizeof(s_net));
        send_all(client, &r_net, sizeof(r_net));

        // recibir respuesta
        uint32_t resp_type;
        if (recv_all(client, &resp_type, sizeof(resp_type)) <= 0) break;
        resp_type = ntohl(resp_type);
        if (resp_type != 2) continue;

        uint32_t rid;
        recv_all(client, &rid, sizeof(rid));
        rid = ntohl(rid);

        uint8_t found;
        recv_all(client, &found, 1);

        if (found) {
            uint64_t nonce_net;
            recv_all(client, &nonce_net, sizeof(nonce_net));
            uint64_t nonce = be64toh(nonce_net);
            unsigned char hash[SHA256_DIGEST_LENGTH];
            recv_all(client, hash, SHA256_DIGEST_LENGTH);

            struct timeval job_end;
            gettimeofday(&job_end, NULL);
            double job_time = (job_end.tv_sec - job_start_times[rid].tv_sec)
                            + (job_end.tv_usec - job_start_times[rid].tv_usec) / 1e6;

            pthread_mutex_lock(&lock);
            if (!done[rid]) {
                done[rid] = 1;
                printf("\n=== JOB %d RESUELTO ===\n", rid);
                printf("Nonce: %lu\n", (unsigned long)nonce);
                printf("Tiempo parcial: %.4f s\n", job_time);
            }


            int all_done = 1;
            for (int i = 0; i < total_jobs; i++)
                if (!done[i]) { all_done = 0; break; }

            if (all_done) {
                gettimeofday(&end_time, NULL);
                double total_time = (end_time.tv_sec - start_time.tv_sec)
                                  + (end_time.tv_usec - start_time.tv_usec) / 1e6;
                printf("\n✅ TODOS LOS BLOQUES RESUELTOS\n");
                printf("Tiempo total: %.4f s\n", total_time);
            }
            pthread_mutex_unlock(&lock);
        }
    }

    close(client);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Uso: %s <puerto> <archivo.txt> [dificultad] [rango]\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    const char *filename = argv[2];
    if (argc >= 4) difficulty = atoi(argv[3]);
    if (argc >= 5) range_size = strtoull(argv[4], NULL, 10);

    FILE *f = fopen(filename, "rb");
    if (!f) { perror("Archivo"); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(sz);
    fread(buf, 1, sz, f);
    fclose(f);

    total_jobs = (sz + BLOCK_SIZE - 1) / BLOCK_SIZE;
    jobs = calloc(total_jobs, sizeof(Job));
    done = calloc(total_jobs, sizeof(int));
    next_nonce = calloc(total_jobs, sizeof(uint64_t));
    job_start_times = calloc(total_jobs, sizeof(struct timeval));

    for (int i = 0; i < total_jobs; i++) {
        jobs[i].id = i;
        jobs[i].len = BLOCK_SIZE;
        jobs[i].data = malloc(BLOCK_SIZE);
        memset(jobs[i].data, ' ', BLOCK_SIZE);
        int start = i * BLOCK_SIZE;
        int len = (sz - start > BLOCK_SIZE) ? BLOCK_SIZE : sz - start;
        memcpy(jobs[i].data, buf + start, len);
    }
    free(buf);

    gettimeofday(&start_time, NULL);

    int server = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = INADDR_ANY;
    bind(server, (struct sockaddr*)&sa, sizeof(sa));
    listen(server, BACKLOG);

    printf("Servidor escuchando en puerto %d\n", port);

    while (1) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int *client = malloc(sizeof(int));
        *client = accept(server, (struct sockaddr*)&cli, &clilen);
        pthread_t t;
        pthread_create(&t, NULL, worker_thread, client);
        pthread_detach(t);
    }
}

