CC     = gcc
CFLAGS = -Wall -Wextra -pthread -g
BDIR   = build
BINDIR = bin

# --- Main commands (used when project is complete) ---
compile: $(BDIR) $(BINDIR) $(BINDIR)/server $(BINDIR)/client

run:
	@echo "--- Starting server (background) ---"
	@./$(BINDIR)/server config/server.conf &
	@sleep 1
	@echo "--- Starting client ---"
	@./$(BINDIR)/client config/client.conf

# --- Create output dirs ---
$(BDIR):
	mkdir -p $(BDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

# --- Compile rules ---
$(BDIR)/config.o: common/config.c common/config.h
	$(CC) $(CFLAGS) -c common/config.c -o $(BDIR)/config.o

$(BDIR)/server.o: server/server.c common/config.h common/protocol.h
	$(CC) $(CFLAGS) -c server/server.c -o $(BDIR)/server.o

$(BDIR)/client.o: client/client.c common/config.h common/protocol.h
	$(CC) $(CFLAGS) -c client/client.c -o $(BDIR)/client.o

# --- Link rules ---
$(BINDIR)/server: $(BDIR)/server.o $(BDIR)/config.o
	$(CC) $(CFLAGS) $(BDIR)/server.o $(BDIR)/config.o -o $(BINDIR)/server

$(BINDIR)/client: $(BDIR)/client.o $(BDIR)/config.o
	$(CC) $(CFLAGS) $(BDIR)/client.o $(BDIR)/config.o -o $(BINDIR)/client

# --- Tests (compile + run, one per step) ---
test_config_loading: compile
	@echo "=== TEST: Config loading (Step 1) ==="
	@echo "--- Server reads config ---"
	@./$(BINDIR)/server config/server.conf
	@echo ""
	@echo "--- Client reads config ---"
	@./$(BINDIR)/client config/client.conf

# --- Clean ---
clean:
	rm -rf $(BDIR) $(BINDIR) logs/*.log