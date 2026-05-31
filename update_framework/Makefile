CC     = gcc
CFLAGS = -Wall -Wextra -pthread -g
BDIR   = build
BINDIR = bin

compile: $(BDIR) $(BINDIR) $(BINDIR)/server $(BINDIR)/client

$(BDIR):
	mkdir -p $(BDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BDIR)/config.o: common/config.c common/config.h
	$(CC) $(CFLAGS) -c common/config.c -o $(BDIR)/config.o

$(BDIR)/logger.o: server/logger.c server/logger.h
	$(CC) $(CFLAGS) -c server/logger.c -o $(BDIR)/logger.o

$(BDIR)/thread_pool.o: server/thread_pool.c server/thread_pool.h
	$(CC) $(CFLAGS) -c server/thread_pool.c -o $(BDIR)/thread_pool.o

$(BDIR)/version_store.o: server/version_store.c server/version_store.h
	$(CC) $(CFLAGS) -c server/version_store.c -o $(BDIR)/version_store.o

$(BDIR)/client_handler.o: server/client_handler.c server/client_handler.h server/logger.h server/version_store.h
	$(CC) $(CFLAGS) -c server/client_handler.c -o $(BDIR)/client_handler.o

$(BDIR)/server.o: server/server.c common/config.h common/protocol.h server/logger.h server/thread_pool.h server/client_handler.h server/version_store.h
	$(CC) $(CFLAGS) -c server/server.c -o $(BDIR)/server.o

$(BDIR)/client.o: client/client.c common/config.h common/protocol.h
	$(CC) $(CFLAGS) -c client/client.c -o $(BDIR)/client.o

$(BINDIR)/server: $(BDIR)/server.o $(BDIR)/config.o $(BDIR)/logger.o $(BDIR)/thread_pool.o $(BDIR)/client_handler.o $(BDIR)/version_store.o
	$(CC) $(CFLAGS) $^ -o $@ -lssl -lcrypto

$(BINDIR)/client: $(BDIR)/client.o $(BDIR)/config.o
	$(CC) $(CFLAGS) $^ -o $@ -lssl -lcrypto

clean:
	rm -rf $(BDIR) $(BINDIR) logs/*.log