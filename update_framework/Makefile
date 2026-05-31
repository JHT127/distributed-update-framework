CC     = gcc
CFLAGS = -Wall -Wextra -pthread -g

# Object files
CONFIG_OBJ  = common/config.o
SERVER_OBJS = server/server.o $(CONFIG_OBJ)
CLIENT_OBJS = client/client.o $(CONFIG_OBJ)

all: bin_server bin_client

# Compile rules
common/config.o: common/config.c common/config.h
	$(CC) $(CFLAGS) -c common/config.c -o common/config.o

server/server.o: server/server.c common/config.h common/protocol.h
	$(CC) $(CFLAGS) -c server/server.c -o server/server.o

client/client.o: client/client.c common/config.h common/protocol.h
	$(CC) $(CFLAGS) -c client/client.c -o client/client.o

# Link rules
bin_server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) $(SERVER_OBJS) -o bin_server

bin_client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) $(CLIENT_OBJS) -o bin_client

# Run both and show output
test: all
	@echo "--- SERVER OUTPUT ---"
	@./bin_server config/server.conf
	@echo ""
	@echo "--- CLIENT OUTPUT ---"
	@./bin_client config/client.conf

clean:
	rm -f bin_server bin_client common/*.o server/*.o client/*.o logs/*.log