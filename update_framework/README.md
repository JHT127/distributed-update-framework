# Distributed Software Update Framework

ENCS4330 — Project 3 | Sockets & POSIX Threads

Client/server system where clients connect, send their version number, and receive the update file if they're outdated. Supports concurrent clients via a thread pool, with auth, checksum verification, and download resume.

---

## Build & Run

**Always run from inside `update_framework/`.**

```bash
make compile          # build both binaries into bin/
make clean            # wipe build, logs, and /tmp/updates/update_v2.pkg

./bin/server config/server.conf   # terminal 1
./bin/client config/client.conf   # terminal 2
```

---

## File Overview

```
common/
  protocol.h          shared message structs (VersionRequest, UpdateResponse, AuthRequest, AuthResponse)
  config.c/h          parse_config() — reads KEY=VALUE from any .conf file

server/
  server.c            entry point — loads config, creates socket, accept loop, dispatches to thread pool
  thread_pool.c/h     fixed-size worker pool with mutex + condition variables; circular task queue
  client_handler.c/h  handle_client() — auth → version check → file transfer (runs in a pool thread)
  version_store.c/h   holds latest version, update file path, and valid token list in memory
  logger.c/h          thread-safe logger — writes timestamped entries to file + stdout

client/
  client.c            loads config, calls CheckForUpdate() with retry loop
                      CheckForUpdate → auth → send version+offset → receive file → MD5 verify
                      getCurrentVersion() returns the version loaded from config

config/
  server.conf         PORT, LATEST_VERSION, UPDATE_FILE, LOG_FILE, THREAD_POOL_SIZE, TOKENS_FILE
  client.conf         SERVER_IP, SERVER_PORT, CURRENT_VERSION, DOWNLOAD_DIR, AUTH_TOKEN, MAX_RETRIES, RETRY_DELAY_SECONDS
  tokens.txt          one valid token per line

update_packages/
  update_v2.pkg       the file served to outdated clients (replace with any file to test large transfers)

logs/
  server.log          written at runtime — format: [timestamp] [LEVEL] [TID:N] [client_ip] message
```

---

## How a connection works (in order)

1. Client connects → sends `AuthRequest` (token string)
2. Server checks token against `tokens.txt` → sends `AuthResponse` (accepted/rejected)
3. Client sends `VersionRequest` (current version + resume offset)
4. Server compares version to latest:
   - **Up to date** → sends `UpdateResponse` with `update_available=0`, closes
   - **Outdated** → computes MD5, fills `UpdateResponse` (file size, filename, checksum), streams file bytes from the resume offset
5. Client receives bytes, appends to local file, re-computes MD5, compares → simulates install

---

## What's been tested ✅

| Scenario                           | Result                                                   |
| ---------------------------------- | -------------------------------------------------------- |
| Outdated client (v1, server v2)    | File downloaded, checksum verified                       |
| Up-to-date client (v2)             | "already up to date" message                             |
| Wrong auth token                   | Rejected immediately, server stays running               |
| 3 concurrent clients               | All served simultaneously (different TID in logs)        |
| Mid-transfer disconnect            | Partial file saved, resumes from correct offset on retry |
| Retry when server is down          | Retries MAX_RETRIES times with RETRY_DELAY_SECONDS gap   |
| Checksum mismatch (corrupted file) | Detected, file deleted, clean retry succeeds             |

---

## What's left for partners

### OpenGL Dashboard (`visualizer/dashboard.c`)

Create a new file `visualizer/dashboard.c`, add it to the Makefile under the server's sources, and link with `-lglut -lGL -lGLU`. The server already has all the data you need — just read from the logger and thread pool. Suggested panels:

- Active connections + total served (text counters)
- One bar per thread showing idle/busy state
- Scrolling last-N log lines
- Connection count over time (simple bar chart)

Run the dashboard in its own thread spawned from `server.c` before the accept loop.

### Proper test scenarios (project spec §8)

Run and document each of these:

- Single client outdated / up-to-date
- 8 simultaneous clients
- Interrupted connection + resume
- Large file transfer (put a 50MB file in `update_packages/`)
- Invalid client requests (send garbage version numbers)
- Pool exhaustion (more clients than `THREAD_POOL_SIZE`)

---

## Notes

- All config values are runtime-loaded — no hardcoded ports, versions, or paths. Change `server.conf` or `client.conf` and rerun without recompiling.
- The server must be started from `update_framework/` because paths like `logs/server.log` and `update_packages/update_v2.pkg` are relative.
- MD5 is used for checksums (OpenSSL). Compile requires `-lssl -lcrypto`.
