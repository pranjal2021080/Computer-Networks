// server_event.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define SERVER_PORT  5432
#define MAX_PENDING  5
#define MAX_LINE     256
#define MAX_CLIENTS  10   // support up to 10 clients

int main() {
    struct sockaddr_in sin;
    int listen_sock, client_socks[MAX_CLIENTS];
    socklen_t addr_len = sizeof(sin);
    fd_set readfds;
    int max_fd;
    char buf[MAX_LINE];

    // Initialize client sockets
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_socks[i] = -1;
    }

    // Build address data structure
    bzero((char *)&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(SERVER_PORT);

    if ((listen_sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(1);
    }

    if (bind(listen_sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("bind failed");
        exit(1);
    }

    if (listen(listen_sock, MAX_PENDING) < 0) {
        perror("listen failed");
        exit(1);
    }

    printf("Event-based Server listening on port %d...\n", SERVER_PORT);

    while (1) {
        FD_ZERO(&readfds);

        // Add listening socket
        FD_SET(listen_sock, &readfds);
        max_fd = listen_sock;

        // Add STDIN
        FD_SET(STDIN_FILENO, &readfds);
        if (STDIN_FILENO > max_fd) max_fd = STDIN_FILENO;

        // Add active clients
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_socks[i] != -1) {
                FD_SET(client_socks[i], &readfds);
                if (client_socks[i] > max_fd)
                    max_fd = client_socks[i];
            }
        }

        // Wait for activity
        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("select error");
            continue;
        }

        // New connection
        if (FD_ISSET(listen_sock, &readfds)) {
            int new_sock = accept(listen_sock, (struct sockaddr *)&sin, &addr_len);
            if (new_sock >= 0) {
                printf("New client connected. Socket: %d\n", new_sock);
                int added = 0;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (client_socks[i] == -1) {
                        client_socks[i] = new_sock;
                        added = 1;
                        break;
                    }
                }
                if (!added) {
                    printf("Too many clients. Closing socket %d\n", new_sock);
                    close(new_sock);
                }
            }
        }

        // Input from server STDIN
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (fgets(buf, sizeof(buf), stdin) != NULL) {
                buf[MAX_LINE - 1] = '\0';

                // Send to one random client (or first active client)
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (client_socks[i] != -1) {
                        send(client_socks[i], buf, strlen(buf) + 1, 0);
                        break; // only send to one
                    }
                }
            }
        }

        // Handle messages from clients
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sock = client_socks[i];
            if (sock != -1 && FD_ISSET(sock, &readfds)) {
                int bytes = recv(sock, buf, sizeof(buf), 0);
                if (bytes <= 0) {
                    printf("[Client %d] Disconnected.\n", sock);
                    close(sock);
                    client_socks[i] = -1;
                } else {
                    buf[MAX_LINE - 1] = '\0';
                    printf("[Client %d]: %s\n", sock, buf);
                }
            }
        }
    }

    close(listen_sock);
    return 0;
}
