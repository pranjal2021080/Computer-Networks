
default: client server

server: server.c
	gcc -Wall -Werror -O3 -o $@ $^ -lpthread

client: client.c
	gcc -Wall -Werror -O3 -o $@ $^

clean:
	rm -f server client
