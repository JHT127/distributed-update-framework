CC     = gcc
CFLAGS = -Wall -Wextra -pthread -g
BDIR   = build

all: $(BDIR) bin_server bin_client

$(BDIR):
	mkdir -p $(BDIR)

# Compile rules — all .o go into build/
$(BDIR)/config.o: common/config.c common/config.h
	$(CC) $(CFLAGS) -c common/config.c -o $(BDIR)/config.o

$(BDIR)/server.o: server/server.c common/config.h common/protocol.h
	$(CC) $(CFLAGS) -c server/server.c -o $(BDIR)/server.o

$(BDIR)/client.o: client/client.c common/config.h common/protocol.h
	$(CC) $(CFLAGS) -c client/client.c -o $(BDIR)/client.o

# Link rules
bin_server: $(BDIR)/server.o $(BDIR)/config.o
	$(CC) $(CFLAGS) $(BDIR)/server.o $(BDIR)/config.o -o bin_server

bin_client: $(BDIR)/client.o $(BDIR)/config.o
	$(CC) $(CFLAGS) $(BDIR)/client.o $(BDIR)/config.o -o bin_client

test: all
	@echo "--- SERVER OUTPUT ---"
	@./bin_server config/server.conf
	@echo ""
	@echo "--- CLIENT OUTPUT ---"
	@./bin_client config/client.conf

# Wipes everything generated
clean:
	rm -rf $(BDIR) bin_server bin_client logs/*.log