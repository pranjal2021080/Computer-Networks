#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "server.h"

// DO NOT define client_socks or valid_ids here!
// They are defined in server.c

/*-------------------------------------------------
 * Function: send_message
 *-------------------------------------------------*/
void send_message(char *msg, int len, int dst_id, int src_id) {
    assert(dst_id < MAX_CLIENTS);
    if (client_socks[dst_id] == INVALID_FD) return;

    char buf[MAX_BUF_SIZE];
    if (src_id == SERVER_ID) {
        snprintf(buf, sizeof(buf), "server: %s", msg);
    } else {
        snprintf(buf, sizeof(buf), "client-%d: %s", src_id, msg);
    }

    if (send(client_socks[dst_id], buf, strlen(buf) + 1, 0) < 0) {
        perror("send error");
        close(client_socks[dst_id]);
        client_socks[dst_id] = INVALID_FD;
        valid_ids[dst_id] = INVALID_ID;
    }
}

/*-------------------------------------------------
 * Function: recv_message
 *-------------------------------------------------*/
void recv_message(char *msg, int len, int client_id, int *valid_ids_local, int num_clients) {
      printf("DEBUG: Server received from client %d: '%s'\n", client_id, msg);
    while (*msg == ' ') msg++; // trim spaces

    if (strncmp(msg, "LIST", 4) == 0) {
        char response[1024];
        int pos = snprintf(response, sizeof(response), "Active clients: ");
        for (int i = 0; i < num_clients; i++) {
            if (valid_ids_local[i] != INVALID_ID) {
                pos += snprintf(response + pos, sizeof(response) - pos, "%d ", i);
            }
        }
        snprintf(response + pos, sizeof(response) - pos, "\n");
        send_message(response, strlen(response) + 1, client_id, SERVER_ID);
        printf("client %d requested LIST -> sent list: %s\n", client_id, response);
        return;
    }

    if (strncmp(msg, "DATA", 4) == 0 && isspace((unsigned char)msg[4])) {
        char *colon = strchr(msg, ':');
        if (!colon) {
            char err[] = "ERROR: DATA missing ':'\n";
            send_message(err, strlen(err), client_id, SERVER_ID);
            return;
        }

        *colon = '\0';
        char *body = colon + 1;
        while (*body && isspace((unsigned char)*body)) body++;

        int num_receivers = 0;
        int ids[100];
        char *token = strtok(msg + 5, " ");
        if (!token) return;
        num_receivers = atoi(token);

        for (int i = 0; i < num_receivers; i++) {
            token = strtok(NULL, " ");
            if (!token) break;
            ids[i] = atoi(token);
        }

        int sent_count = 0;
        char invalid_ids[256] = "";
        for (int i = 0; i < num_receivers; i++) {
            int dst = ids[i];
            if (dst >= 0 && dst < num_clients && valid_ids_local[dst] != INVALID_ID) {
                send_message(body, strlen(body) + 1, dst, client_id);
                sent_count++;
            } else {
                char tmp[16];
                snprintf(tmp, sizeof(tmp), " %d", dst);
                strncat(invalid_ids, tmp, sizeof(invalid_ids) - strlen(invalid_ids) - 1);
            }
        }

        char ack[256];
        if (strlen(invalid_ids) == 0)
            snprintf(ack, sizeof(ack), "Sent to %d recipient(s)\n", sent_count);
        else
            snprintf(ack, sizeof(ack), "Sent to %d recipient(s); invalid ids:%s\n", sent_count, invalid_ids);

        send_message(ack, strlen(ack), client_id, SERVER_ID);
        printf("client %d sent DATA to %d clients: %s\n", client_id, sent_count, body);
        return;
    }

    char err[] = "ERROR: unknown command. Use LIST or DATA <N> ids...: message\n";
    send_message(err, strlen(err), client_id, SERVER_ID);
    printf("client %d sent unrecognized message: %s\n", client_id, msg);
}

