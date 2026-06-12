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
#  TEST CONFIG FILES  (created once, reused by every test target)
# ═══════════════════════════════════════════════════════════════════════

config/client_uptodate.conf:
	@printf 'SERVER_IP=127.0.0.1\nSERVER_PORT=9000\nCURRENT_VERSION=2\nDOWNLOAD_DIR=/tmp/updates\nAUTH_TOKEN=mysecrettoken123\nMAX_RETRIES=3\nRETRY_DELAY_SECONDS=3\n' > $@

config/client_badauth.conf:
	@printf 'SERVER_IP=127.0.0.1\nSERVER_PORT=9000\nCURRENT_VERSION=1\nDOWNLOAD_DIR=/tmp/updates\nAUTH_TOKEN=wrongtoken\nMAX_RETRIES=1\nRETRY_DELAY_SECONDS=1\n' > $@

config/client_future.conf:
	@printf 'SERVER_IP=127.0.0.1\nSERVER_PORT=9000\nCURRENT_VERSION=99\nDOWNLOAD_DIR=/tmp/updates\nAUTH_TOKEN=mysecrettoken123\nMAX_RETRIES=3\nRETRY_DELAY_SECONDS=3\n' > $@

# ═══════════════════════════════════════════════════════════════════════
#  VISUALIZATION TEST TARGETS
#  Run these in a second terminal while  make run-server  is running.
#
#  test-outdated      — TC1: one outdated client  (v1 → downloads update)
#  test-uptodate      — TC2: one up-to-date client (v2 → no download)
#  test-multi         — TC3: 8 clients at once    (fills all thread bars)
#  test-mixed         — TC4: 4 outdated + 4 up-to-date simultaneously
#  test-badauth       — TC5: wrong token           (AUTH FAIL badge)
#  test-resume        — TC6: large file for resume testing
#  test-largefile     — TC7: 50 MB transfer        (slow progress bar)
#  test-poolexhaust   — TC8: 20 clients on 4 threads (queue visible)
#  test-future        — TC9: version 99            (treated as up-to-date)
#  test-demo          — runs TC1+TC2+TC5 together  (all panels at once)
# ═══════════════════════════════════════════════════════════════════════

# TC1 — single outdated client: watch one thread go AUTH→VERSION→TRANSFER→DONE
test-outdated: $(BINDIR)/client
	@echo "[TC1] Outdated client — watch the dashboard: one thread fills orange then green"
	@rm -f /tmp/updates/update_v2.pkg
	./$(BINDIR)/client config/client.conf

# TC2 — single up-to-date client: thread goes AUTH→VERSION→DONE instantly, UP-TO-DATE badge +1
test-uptodate: $(BINDIR)/client config/client_uptodate.conf
	@echo "[TC2] Up-to-date client — watch UP-TO-DATE badge increment, no transfer bar"
	./$(BINDIR)/client config/client_uptodate.conf

# TC3 — 8 clients at once: all thread bars light up simultaneously in different states
test-multi: $(BINDIR)/client
	@echo "[TC3] 8 simultaneous outdated clients — watch all thread bars fill at once"
	@rm -f /tmp/updates/update_v2.pkg
	@dd if=/dev/urandom of=update_packages/update_v2.pkg bs=1M count=10 2>/dev/null
	@for i in 1 2 3 4 5 6 7 8; do ./$(BINDIR)/client config/client.conf & done; wait

# TC4 — 4 outdated + 4 up-to-date: UPDATED and UP-TO-DATE badges both increment
test-mixed: $(BINDIR)/client config/client_uptodate.conf
	@echo "[TC4] Mixed clients — watch both UPDATED and UP-TO-DATE badges increment"
	@rm -f /tmp/updates/update_v2.pkg
	@for i in 1 2 3 4; do ./$(BINDIR)/client config/client.conf & done
	@for i in 1 2 3 4; do ./$(BINDIR)/client config/client_uptodate.conf & done
	@wait

# TC5 — bad auth token: AUTH FAIL badge increments, red WARN line in event log
test-badauth: $(BINDIR)/client config/client_badauth.conf
	@echo "[TC5] Bad auth token — watch AUTH FAIL badge and red WARN in event log"
	./$(BINDIR)/client config/client_badauth.conf

# TC6 — resume: creates a large file then you kill/restart the server mid-transfer
test-resume: $(BINDIR)/client
	@echo "[TC6] Resume test — start download, then kill+restart the server mid-transfer"
	@echo "      The client will retry and resume from where it left off"
	@rm -f /tmp/updates/update_v2.pkg
	@dd if=/dev/urandom of=update_packages/update_v2.pkg bs=1M count=20 2>/dev/null
	./$(BINDIR)/client config/client.conf

# TC7 — large file: 50 MB so the orange progress bar fills slowly with live %
test-largefile: $(BINDIR)/client
	@echo "[TC7] Large file (50 MB) — watch the progress bar fill slowly with % counter"
	@rm -f /tmp/updates/update_v2.pkg
	@dd if=/dev/urandom of=update_packages/update_v2.pkg bs=1M count=50 2>/dev/null
	./$(BINDIR)/client config/client.conf

# TC8 — pool exhaustion: 20 clients on 4 threads, rest queue up visibly
test-poolexhaust: $(BINDIR)/client
	@echo "[TC8] Pool exhaustion — 20 clients on 4 threads, watch queue drain one by one"
	@echo "      NOTE: restart server with THREAD_POOL_SIZE=4 in config/server.conf first"
	@for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do \
		./$(BINDIR)/client config/client.conf & \
	done; wait

# TC9 — future version (v99): server says up-to-date, no crash, UP-TO-DATE badge +1
test-future: $(BINDIR)/client config/client_future.conf
	@echo "[TC9] Future version (v99) — server treats it as up-to-date, no crash"
	./$(BINDIR)/client config/client_future.conf

# DEMO — fires TC1 + TC2 + TC5 all at once: all dashboard panels active simultaneously
test-demo: $(BINDIR)/client config/client_uptodate.conf config/client_badauth.conf
	@echo "[DEMO] All scenarios at once — all panels and badges updating simultaneously"
	@rm -f /tmp/updates/update_v2.pkg
	@dd if=/dev/urandom of=update_packages/update_v2.pkg bs=1M count=10 2>/dev/null
	@for i in 1 2 3 4; do ./$(BINDIR)/client config/client.conf & done
	@for i in 1 2;     do ./$(BINDIR)/client config/client_uptodate.conf & done
	@./$(BINDIR)/client config/client_badauth.conf &
	@wait

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

$(BDIR)/config.o: common/config.c common/config.h
	$(CC) $(CFLAGS) $(SSL_INC) -c common/config.c -o $(BDIR)/config.o

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

.PHONY: compile run-server run-client clean \
        test-outdated test-uptodate test-multi test-mixed \
        test-badauth test-resume test-largefile test-poolexhaust \
        test-future test-demo
