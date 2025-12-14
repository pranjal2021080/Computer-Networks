// server.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <assert.h>

#define SERVER_PORT  5432
#define MAX_PENDING  5
#define MAX_LINE     256

// This function handles communication with a single client
void event_loop(int client_sock) {
    fd_set readfds;
    struct timeval tv;
    int max_fd = STDIN_FILENO;
    int activity;
    char buf[MAX_LINE];

    if (client_sock > max_fd) {
        max_fd = client_sock;
    }

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(client_sock, &readfds);

        tv.tv_sec = 30;
        tv.tv_usec = 0;

        activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

        if (activity < 0) {
            perror("select");
            break;
        } else if (activity == 0) {
            printf("[Client %d] No activity for 30 seconds.\n", client_sock);
            continue;
        }

        if (FD_ISSET(client_sock, &readfds)) {
            int bytes = recv(client_sock, buf, sizeof(buf), 0);
            buf[MAX_LINE - 1] = '\0';
            if (bytes <= 0) {
                printf("[Client %d] Disconnected.\n", client_sock);
                close(client_sock);
                return;
            }
            printf("[Client %d]: %s\n", client_sock, buf);
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (fgets(buf, sizeof(buf), stdin) != NULL) {
                buf[MAX_LINE - 1] = '\0';
                if (send(client_sock, buf, strlen(buf) + 1, 0) < 0) {
                    perror("send error");
                    close(client_sock);
                    return;
                }
            }
        }
    }

    close(client_sock);
}

// Thread function to handle each client
void* client_thread(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);
    event_loop(client_sock);
    return NULL;
}

int main() {
    struct sockaddr_in sin;
    socklen_t addr_len = sizeof(sin);
    int s;

    // Build address data structure
    bzero((char *)&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(SERVER_PORT);

    // Setup passive open
    if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(1);
    }

    if ((bind(s, (struct sockaddr *)&sin, sizeof(sin))) < 0) {
        perror("bind failed");
        exit(1);
    }

    if (listen(s, MAX_PENDING) < 0) {
        perror("listen failed");
        exit(1);
    }

    printf("Server listening on port %d...\n", SERVER_PORT);

    // Accept and spawn threads for each client
    while (1) {
        int* new_s = malloc(sizeof(int));
        if (!new_s) {
            perror("malloc failed");
            continue;
        }

        *new_s = accept(s, (struct sockaddr *)&sin, &addr_len);
        if (*new_s < 0) {
            perror("accept failed");
            free(new_s);
            continue;
        }

        printf("New client connected. Socket: %d\n", *new_s);

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, new_s) != 0) {
            perror("pthread_create failed");
            close(*new_s);
            free(new_s);
        } else {
            pthread_detach(tid);  // No need to join
        }
    }

    close(s);
    return 0;
}
