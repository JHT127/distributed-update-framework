CC = gcc
CFLAGS = -Wall -Wextra -pthread -g

SERVER_SRCS = server/server.c common/config.c
CLIENT_SRCS = client/client.c common/config.c

all: server client

server: $(SERVER_SRCS) common/protocol.h common/config.h
	$(CC) $(CFLAGS) $(SERVER_SRCS) -o server

client: $(CLIENT_SRCS) common/protocol.h common/config.h
	$(CC) $(CFLAGS) $(CLIENT_SRCS) -o client

clean:
	rm -f server client logs/*.log