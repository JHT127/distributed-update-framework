# Distributed Software Update Framework

ENCS4330 — Project 3 | Sockets & POSIX Threads

Client/server system where clients connect, send their version number, and receive the update file if they're outdated. Supports concurrent clients via a thread pool, with auth, checksum verification, and download resume.

---

## Build & Run

**Run commands from the repository root.**

# Distributed Software Update Framework

An educational C client/server system for delivering versioned update packages over TCP. It demonstrates POSIX threads, a bounded worker queue, token checks, resumable transfers, checksum verification, and a live OpenGL server dashboard.

[![CI](https://github.com/JHT127/real-time-project-three/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/JHT127/real-time-project-three/actions/workflows/ci.yml)
[![Live protocol demo](https://img.shields.io/badge/demo-GitHub%20Pages-286f62)](https://jht127.github.io/real-time-project-three/)

## Live Demo

**[Open the interactive protocol demo](https://jht127.github.io/real-time-project-three/)**

The Pages demo is a browser-side simulation of the client/server exchange. It does not connect to or host the C server; run the project locally to use the real TCP implementation.

## Features

- Concurrent client handling with a POSIX thread pool and bounded queue.
- Token-based authentication using a local tokens file.
- Version comparison and package transfer from a configurable file path.
- Resume offsets for interrupted downloads and MD5 checksums for transfer integrity.
- OpenGL dashboard for connection, worker, transfer, and log activity.
- Headless dashboard-state tests that run without opening a graphics window.

## Requirements

- Linux or macOS with a C compiler, GNU Make, and POSIX threads.
- OpenSSL development headers and libraries.
- OpenGL, GLU, and GLUT development libraries (macOS uses the system OpenGL/GLUT frameworks).

Windows users should build inside WSL or another POSIX environment; the server depends on POSIX sockets and headers.

## Build and Run

Run commands from the repository root. Start the server in one terminal; it opens the OpenGL dashboard in a graphical session. Start the client in another terminal.

```sh
make compile
make run-server
```

```sh
make run-client
```

The default client config reports version `1`; the server advertises version `2`. The sample package is 50 MiB, so the first client run may take a little while. Downloads are written to `/tmp/updates/` by default.

To remove generated binaries, object files, and local logs:

```sh
make clean
```

## Tests

Run the headless dashboard state suite:

```sh
make test-dashboard
```

The manual integration targets in the Makefile cover outdated and current clients, invalid authentication, concurrent clients, transfers, resume behavior, and pool exhaustion. Start the server before running those client scenarios. `make test-poolexhaust` stops and restarts a local server, so use it only in a dedicated development environment. The automated CI job builds both binaries and runs the headless suite; it does not exercise live networking or open an OpenGL window.

## Configuration

Edit the sample files under `config/`:

- `server.conf`: listening port, latest version, package path, log path, worker count, and tokens path.
- `client.conf`: server address, client version, download directory, sample token, and retry settings.
- `tokens.txt`: one accepted sample token per line.

Paths are relative to the repository root unless they begin with `/`. These credentials are examples for local testing; replace them for your own environment.

## Protocol Outline

1. The client sends an `AuthRequest`; the server accepts or rejects its token.
2. An authenticated client sends its version and current resume offset.
3. The server sends an `UpdateResponse`. An up-to-date client closes without downloading.
4. An outdated client receives the remaining package bytes, appends them to its partial file, and checks the completed file's MD5 digest.

## Repository Layout

```text
client/          TCP client
common/          shared configuration and protocol definitions
config/          local example configurations and tokens
server/          TCP server, worker pool, transfer handler, and logger
tests/           headless dashboard tests and test stub
update_packages/ sample package served to clients
visualizer/      OpenGL dashboard
docs/            static GitHub Pages demonstration
```

## Security and Scope

This is a learning project, not a production update service. The TCP protocol has no TLS, sample tokens are sent in plaintext, and MD5 is used only as a corruption check, not as an authenticity or signature mechanism. The server binds to all network interfaces by default. Do not expose it to untrusted networks or use it to distribute real updates without adding transport security, signed packages, robust input validation, and deployment controls.

No license is included yet. Public visibility does not grant permission to reuse or redistribute the code; add a license before accepting outside contributions or inviting reuse.
