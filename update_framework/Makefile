CC     = gcc
CFLAGS = -Wall -Wextra -pthread -g
BDIR   = build
BINDIR = bin

SERVER_CONF = config/server.conf
CLIENT_CONF = config/client.conf

# ── detect OS for OpenGL and OpenSSL ───────────────────────────────────
UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
	OPENSSL_PREFIX = $(shell brew --prefix openssl 2>/dev/null)
	SSL_INC  = -I$(OPENSSL_PREFIX)/include
	SSL_LIBS = -L$(OPENSSL_PREFIX)/lib -lssl -lcrypto
	GL_LIBS  = -framework OpenGL -framework GLUT -framework Cocoa
else
	SSL_INC  =
	SSL_LIBS = -lssl -lcrypto
	GL_LIBS  = -lGL -lGLU -lglut -lm
endif

# ═══════════════════════════════════════════════════════════════════════
#  FRIENDLY TARGETS
# ═══════════════════════════════════════════════════════════════════════

compile: $(BDIR) $(BINDIR) $(BINDIR)/server $(BINDIR)/client

run-server: $(BINDIR)/server
	./$(BINDIR)/server $(SERVER_CONF)

run-client: $(BINDIR)/client
	./$(BINDIR)/client $(CLIENT_CONF)

clean:
	rm -rf $(BDIR) $(BINDIR) logs/*.log /tmp/updates/update_v2.pkg

# ═══════════════════════════════════════════════════════════════════════
#  DIRECTORIES
# ═══════════════════════════════════════════════════════════════════════

$(BDIR):
	mkdir -p $(BDIR)

$(BINDIR):
	mkdir -p $(BINDIR)
	mkdir -p /tmp/updates

# ═══════════════════════════════════════════════════════════════════════
#  OBJECT FILES
# ═══════════════════════════════════════════════════════════════════════

# ── common ─────────────────────────────────────────────────────────────
$(BDIR)/config.o: common/config.c common/config.h
	$(CC) $(CFLAGS) $(SSL_INC) -c common/config.c -o $(BDIR)/config.o

# ── server modules ──────────────────────────────────────────────────────
$(BDIR)/logger.o: server/logger.c server/logger.h visualizer/dashboard.h
	$(CC) $(CFLAGS) $(SSL_INC) -c server/logger.c -o $(BDIR)/logger.o

$(BDIR)/thread_pool.o: server/thread_pool.c server/thread_pool.h
	$(CC) $(CFLAGS) -c server/thread_pool.c -o $(BDIR)/thread_pool.o

$(BDIR)/version_store.o: server/version_store.c server/version_store.h
	$(CC) $(CFLAGS) $(SSL_INC) -c server/version_store.c -o $(BDIR)/version_store.o

$(BDIR)/client_handler.o: server/client_handler.c server/client_handler.h \
                           server/logger.h server/version_store.h \
                           visualizer/dashboard.h
	$(CC) $(CFLAGS) $(SSL_INC) -c server/client_handler.c -o $(BDIR)/client_handler.o

$(BDIR)/dashboard.o: visualizer/dashboard.c visualizer/dashboard.h
	$(CC) $(CFLAGS) -c visualizer/dashboard.c -o $(BDIR)/dashboard.o

$(BDIR)/server.o: server/server.c common/config.h common/protocol.h \
                  server/logger.h server/thread_pool.h server/client_handler.h \
                  server/version_store.h visualizer/dashboard.h
	$(CC) $(CFLAGS) $(SSL_INC) -c server/server.c -o $(BDIR)/server.o

# ── client ──────────────────────────────────────────────────────────────
$(BDIR)/client.o: client/client.c common/config.h common/protocol.h
	$(CC) $(CFLAGS) $(SSL_INC) -c client/client.c -o $(BDIR)/client.o

# ═══════════════════════════════════════════════════════════════════════
#  LINK
# ═══════════════════════════════════════════════════════════════════════

$(BINDIR)/server: $(BDIR)/server.o $(BDIR)/config.o $(BDIR)/logger.o \
                  $(BDIR)/thread_pool.o $(BDIR)/client_handler.o \
                  $(BDIR)/version_store.o $(BDIR)/dashboard.o
	$(CC) $(CFLAGS) $^ -o $@ $(SSL_LIBS) $(GL_LIBS)

$(BINDIR)/client: $(BDIR)/client.o $(BDIR)/config.o
	$(CC) $(CFLAGS) $^ -o $@ $(SSL_LIBS)

.PHONY: compile run-server run-client clean