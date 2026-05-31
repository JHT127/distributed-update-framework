CC     = gcc
CFLAGS = -Wall -Wextra -pthread -g
BDIR   = build
BINDIR = bin

compile: $(BDIR) $(BINDIR) $(BINDIR)/server $(BINDIR)/client

run:
	@echo "--- Starting server (background) ---"
	@./$(BINDIR)/server config/server.conf &
	@sleep 1
	@echo "--- Starting client ---"
	@./$(BINDIR)/client config/client.conf

$(BDIR):
	mkdir -p $(BDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BDIR)/config.o: common/config.c common/config.h
	$(CC) $(CFLAGS) -c common/config.c -o $(BDIR)/config.o

$(BDIR)/logger.o: server/logger.c server/logger.h
	$(CC) $(CFLAGS) -c server/logger.c -o $(BDIR)/logger.o

$(BDIR)/server.o: server/server.c common/config.h common/protocol.h server/logger.h
	$(CC) $(CFLAGS) -c server/server.c -o $(BDIR)/server.o

$(BDIR)/client.o: client/client.c common/config.h common/protocol.h
	$(CC) $(CFLAGS) -c client/client.c -o $(BDIR)/client.o

$(BINDIR)/server: $(BDIR)/server.o $(BDIR)/config.o $(BDIR)/logger.o
	$(CC) $(CFLAGS) $(BDIR)/server.o $(BDIR)/config.o $(BDIR)/logger.o -o $(BINDIR)/server

$(BINDIR)/client: $(BDIR)/client.o $(BDIR)/config.o
	$(CC) $(CFLAGS) $(BDIR)/client.o $(BDIR)/config.o -o $(BINDIR)/client

test_config_loading: compile
	@echo "=== TEST: Config loading (Step 1) ==="
	@echo "--- Server reads config ---"
	@./$(BINDIR)/server config/server.conf
	@echo ""
	@echo "--- Client reads config ---"
	@./$(BINDIR)/client config/client.conf

test_logger: compile
	@echo "=== TEST: Logger (Step 2) ==="
	@./$(BINDIR)/server config/server.conf
	@echo ""
	@echo "--- Log file contents ---"
	@cat logs/server.log

clean:
	rm -rf $(BDIR) $(BINDIR) logs/*.log