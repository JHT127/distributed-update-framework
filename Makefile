CC     = gcc
CFLAGS = -Wall -Wextra -pthread -g
BDIR   = build
BINDIR = bin

SERVER_CONF          = config/server.conf
SERVER_CONF_EXHAUST  = config/server_poolexhaust.conf
CLIENT_CONF          = config/client.conf
TEST_STATE_FILE      = /tmp/update_test_state.txt

define set_current_test
	@mkdir -p /tmp
	@printf '%s\n%s\n' "$(1)" "$(2)" > $(TEST_STATE_FILE)
endef

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

test-dashboard: $(BINDIR)/test_dashboard_states
	./$(BINDIR)/test_dashboard_states

clean:
	rm -rf $(BDIR) $(BINDIR) logs/*.log /tmp/updates/
	rm -f /tmp/update-v2-pool-test.pkg

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
#  test-demo          — runs all TC1–TC9 sequentially (self-contained)
# ═══════════════════════════════════════════════════════════════════════

# TC1 — single outdated client: watch one thread go AUTH→VERSION→TRANSFER→DONE
test-outdated: $(BINDIR)/client
	$(call set_current_test,TC1,Outdated client)
	@echo "[TC1] Outdated client — watch the dashboard: one thread fills orange then green"
	@rm -f /tmp/updates/update_v2.pkg
	./$(BINDIR)/client config/client.conf

# TC2 — single up-to-date client: thread goes AUTH→VERSION→DONE instantly, UP-TO-DATE badge +1
test-uptodate: $(BINDIR)/client config/client_uptodate.conf
	$(call set_current_test,TC2,Up-to-date client)
	@echo "[TC2] Up-to-date client — watch UP-TO-DATE badge increment, no transfer bar"
	./$(BINDIR)/client config/client_uptodate.conf

# TC3 — 8 clients at once: all thread bars light up simultaneously
#        Uses the committed update package so checksums pass.
test-multi: $(BINDIR)/client
	$(call set_current_test,TC3,8 simultaneous outdated clients)
	@echo "[TC3] 8 simultaneous outdated clients — watch all thread bars fill at once"
	@rm -f /tmp/updates/update_v2.pkg
	@for i in 1 2 3 4 5 6 7 8; do ./$(BINDIR)/client config/client.conf & done; wait

# TC4 — 4 outdated + 4 up-to-date: UPDATED and UP-TO-DATE badges both increment
test-mixed: $(BINDIR)/client config/client_uptodate.conf
	$(call set_current_test,TC4,Mixed clients)
	@echo "[TC4] Mixed clients — watch both UPDATED and UP-TO-DATE badges increment"
	@rm -f /tmp/updates/update_v2.pkg
	@for i in 1 2 3 4; do ./$(BINDIR)/client config/client.conf & done; \
	 for i in 1 2 3 4; do ./$(BINDIR)/client config/client_uptodate.conf & done; \
	 wait

# TC5 — bad auth token: AUTH FAIL badge increments, red WARN line in event log
test-badauth: $(BINDIR)/client config/client_badauth.conf
	$(call set_current_test,TC5,Bad auth token)
	@echo "[TC5] Bad auth token — watch AUTH FAIL badge and red WARN in event log"
	./$(BINDIR)/client config/client_badauth.conf

# TC6 — resume: interrupt a 50 MB transfer, then retry the client
test-resume: $(BINDIR)/client
	$(call set_current_test,TC6,Resume test)
	@echo "[TC6] Resume test — start download, then kill+restart the server mid-transfer"
	@echo "      The client will retry and resume from where it left off"
	@rm -f /tmp/updates/update_v2.pkg
	./$(BINDIR)/client config/client.conf

# TC7 — large file: 50 MB so the orange progress bar fills slowly with live %
test-largefile: $(BINDIR)/client
	$(call set_current_test,TC7,Large file transfer)
	@echo "[TC7] Large file (50 MB) — watch the progress bar fill slowly with % counter"
	@rm -f /tmp/updates/update_v2.pkg
	./$(BINDIR)/client config/client.conf

# TC8 — pool exhaustion: 20 clients on 4 threads, rest queue up visibly.
#        Restarts the server with the 4-thread config automatically.
test-poolexhaust: $(BINDIR)/client $(BINDIR)/server
	$(call set_current_test,TC8,Pool exhaustion)
	@echo "[TC8] Pool exhaustion — restarting server with 4-thread config"
	@pkill -f "$(BINDIR)/server" 2>/dev/null || true
	@sleep 1
	@./$(BINDIR)/server $(SERVER_CONF_EXHAUST) &
	@sleep 1
	@rm -f /tmp/updates/update_v2.pkg
	@dd if=/dev/urandom of=/tmp/update-v2-pool-test.pkg bs=1M count=10 2>/dev/null
	@for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do \
		mkdir -p /tmp/updates/client$$i && \
		DOWNLOAD_DIR=/tmp/updates/client$$i ./$(BINDIR)/client config/client.conf & \
	done; wait
	@echo "[TC8] Restoring server with default config"
	@pkill -f "$(BINDIR)/server" 2>/dev/null || true
	@sleep 1
	@rm -f /tmp/update-v2-pool-test.pkg
	@./$(BINDIR)/server $(SERVER_CONF) &
	@sleep 1

# TC9 — future version (v99): server says up-to-date, no crash, UP-TO-DATE badge +1
test-future: $(BINDIR)/client config/client_future.conf
	$(call set_current_test,TC9,Future version test)
	@echo "[TC9] Future version (v99) — server treats it as up-to-date, no crash"
	./$(BINDIR)/client config/client_future.conf

# ═══════════════════════════════════════════════════════════════════════
#  DEMO — runs all scenarios sequentially, fully self-contained.
#
#  Fix 1 (TC3 double-run): the 50 MB package built at the top is reused
#  for TC3; we do NOT overwrite it with a random file mid-demo, so
#  checksums pass on the first attempt and clients never retry.
#
#  TC8 (pool exhaustion) is intentionally excluded — it requires a server
#  restart mid-demo which risks terminating the make process. Run it
#  separately with  make test-poolexhaust  after starting the server with
#  config/server_poolexhaust.conf.
# ═══════════════════════════════════════════════════════════════════════

test-demo: $(BINDIR)/client $(BINDIR)/server \
           config/client_uptodate.conf config/client_badauth.conf config/client_future.conf
	$(call set_current_test,TC-DEMO,Demo sequence)
	@echo "[DEMO] Running all scenarios sequentially — watch each panel update one at a time"

	@# Reuse the committed 50 MB package for every scenario.

	@echo ""; echo "[TC1] Outdated client — one thread fills orange then green"
	$(call set_current_test,TC1,Outdated client)
	@rm -f /tmp/updates/update_v2.pkg
	@./$(BINDIR)/client config/client.conf
	@sleep 3

	@echo ""; echo "[TC2] Up-to-date client — UP-TO-DATE badge +1, no transfer bar"
	$(call set_current_test,TC2,Up-to-date client)
	@./$(BINDIR)/client config/client_uptodate.conf
	@sleep 3

	@# TC3: remove the client's cached file so all 8 clients download fresh.
	@# The server package is NOT regenerated, so MD5 stays consistent.
	@echo ""; echo "[TC3] 8 simultaneous outdated clients — all thread bars fill at once"
	$(call set_current_test,TC3,8 simultaneous outdated clients)
	@rm -f /tmp/updates/update_v2.pkg
	@for i in 1 2 3 4 5 6 7 8; do ./$(BINDIR)/client config/client.conf & done; wait
	@sleep 3

	@echo ""; echo "[TC4] Mixed clients — both UPDATED and UP-TO-DATE badges increment"
	$(call set_current_test,TC4,Mixed clients)
	@rm -f /tmp/updates/update_v2.pkg
	@for i in 1 2 3 4; do ./$(BINDIR)/client config/client.conf & done; \
	 for i in 1 2 3 4; do ./$(BINDIR)/client config/client_uptodate.conf & done; \
	 wait
	@sleep 3

	@echo ""; echo "[TC5] Bad auth token — AUTH FAIL badge and red WARN in event log"
	$(call set_current_test,TC5,Bad auth token)
	@./$(BINDIR)/client config/client_badauth.conf
	@sleep 8

	@echo ""; echo "[TC7] Large file (50 MB) — progress bar fills slowly with live %"
	$(call set_current_test,TC7,Large file transfer)
	@rm -f /tmp/updates/update_v2.pkg
	@./$(BINDIR)/client config/client.conf
	@sleep 3

	@echo ""; echo "[TC9] Future version (v99) — treated as up-to-date, no crash"
	$(call set_current_test,TC9,Future version test)
	@./$(BINDIR)/client config/client_future.conf

	@echo ""; echo "[DEMO] Complete."

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

$(BINDIR)/test_dashboard_states: tests/test_dashboard_states.c tests/dashboard_stub.c visualizer/dashboard.h | $(BINDIR)
	$(CC) $(CFLAGS) tests/test_dashboard_states.c tests/dashboard_stub.c -o $@

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
	 test-dashboard \
        test-outdated test-uptodate test-multi test-mixed \
        test-badauth test-resume test-largefile test-poolexhaust \
        test-future test-demo