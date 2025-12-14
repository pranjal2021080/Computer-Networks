// Source: https://book.systemsapproach.org/foundation/software.html

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define SERVER_PORT 5432
#define MAX_LINE 256

int main(int argc, char * argv[])
{
  struct hostent *hp;
  struct sockaddr_in sin;
  char *host;
  char buf[MAX_LINE];
  int s;
  int len, max_fd = STDIN_FILENO;
	fd_set readfds;
	struct timeval tv;
	int activity;

  if (argc==2) {
    host = argv[1];
  }
  else {
		// default localhost
		host = "localhost";
  }

  /* translate host name into peer's IP address */
  hp = gethostbyname(host);
  if (!hp) {
    fprintf(stderr, "simplex-talk: unknown host: %s\n", host);
    exit(1);
  }

  /* build address data structure */
  bzero((char *)&sin, sizeof(sin));
  sin.sin_family = AF_INET;
  bcopy(hp->h_addr, (char *)&sin.sin_addr, hp->h_length);
  sin.sin_port = htons(SERVER_PORT);

  /* active open */
  if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(1);
  }
  if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
  {
    perror("connect");
    close(s);
    exit(1);
  }
	max_fd = (s > max_fd) ? s : max_fd;

	while(1) {
		FD_ZERO(&readfds);
		FD_SET(s, &readfds);
		FD_SET(STDIN_FILENO, &readfds);

		tv.tv_sec = 30;
		tv.tv_usec = 0;

		activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

		if (activity < 0) {
			perror("select");
			break;
		} else if (activity == 0) {
			printf("No activity for 30 secs.\n");
			continue;
		}

		if (FD_ISSET(STDIN_FILENO, &readfds)) {
			if (fgets(buf, sizeof(buf), stdin) != NULL) {
    		buf[MAX_LINE-1] = '\0';
    		len = strlen(buf) + 1;
  			if (send(s, buf, len, 0) < 0) {
					perror("server unavailable");
					close(s);
					break;
				}
			}
		}

		if (FD_ISSET(s, &readfds)) {
			int bytes = recv(s, buf, sizeof(buf), 0);
			if (bytes <= 0) {
				printf("server disconnected.\n");
				break;
			}
    	buf[MAX_LINE-1] = '\0';
			printf("%s\n", buf);
		}
	}
	close(s);
	return 0;
}
