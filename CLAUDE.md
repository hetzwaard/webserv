# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

This is the **42/Codam `webserv`** project (subject v24.0, `webserv.pdf` at repo root): an HTTP/1.1 server in **C++98** that must run as `./webserv [configuration file]`, accept real browser traffic, and serve at least GET/POST/DELETE over a single-`poll()` event loop. Student login: `mahkilic` (Codam).

## Build & Run

```bash
make            # builds ./webserv (obj/ auto-created)
make re         # fclean + all
make clean      # remove obj/
make fclean     # remove obj/ and binary
./webserv                      # current placeholder — listens blocking on 9090
./webserv configs/default.conf # target invocation once config parsing exists
```

The Makefile enforces `-Wall -Wextra -Werror -std=c++98`; any warning is a build failure. `SRCS` is an explicit list (not a glob) — append new `.cpp` files there. `include/` is referenced via `-I$(INCDIR)` but doesn't exist yet.

No tests or linter configured. The subject explicitly recommends writing testers in Python/Go (not only in C/C++), and comparing behavior against NGINX + `telnet`.

## Current state vs. target

`src/main.cpp` is a ~75-line pedagogical scaffold: `socket → bind(9090) → listen → accept → read → close`, one blocking client, then exits. It is **not yet an HTTP server** — missing: config parser, poll loop, request parser, router, response writer, CGI, uploads, error pages. Every subject requirement below is still to be built.

## Subject constraints (hard rules — violating these means grade 0)

These come from `webserv.pdf` and are non-negotiable.

**Stability**
- The program must never crash or terminate unexpectedly — not even on OOM. A crash → grade 0.
- The server must remain operational indefinitely; a request must never hang forever.

**I/O discipline (most common failure mode)**
- Every socket / pipe / FIFO must be non-blocking and driven through **a single `poll()` (or `select`/`epoll`/`kqueue`)** — one loop covers listen sockets, client sockets, and CGI pipes.
- `poll()` must monitor read AND write readiness simultaneously.
- Never call `read`/`recv`/`write`/`send` on these fds without prior readiness from the poll call.
- Never inspect `errno` after a `read`/`write` to branch server behavior (no `EAGAIN` retry logic based on errno).
- Regular disk files are exempt — you may `read`/`write` them directly without poll.
- `fork()` is only permitted for CGI. No per-connection threads/processes.

**Allowed external functions (everything else is forbidden):**
`execve, pipe, strerror, gai_strerror, errno, dup, dup2, fork, socketpair, htons, htonl, ntohs, ntohl, select, poll, epoll_create, epoll_ctl, epoll_wait, kqueue, kevent, socket, accept, listen, send, recv, chdir, bind, connect, getaddrinfo, freeaddrinfo, setsockopt, getsockname, getprotobyname, fcntl, close, read, write, waitpid, kill, signal, access, stat, open, opendir, readdir, closedir`. No external libraries, no Boost, no libft. Prefer C++ headers (`<cstring>` over `<string.h>`).

**macOS-only carve-out:** `fcntl()` may be used but only with `F_SETFL`, `O_NONBLOCK`, `FD_CLOEXEC`.

## Required features (mandatory part)

- HTTP methods: at minimum **GET, POST, DELETE**, with accurate status codes.
- Default error pages when none configured.
- Serve a fully static website; support client file uploads.
- Listen on **multiple `interface:port` pairs** from config; each may serve different content.
- CGI execution dispatched by file extension (at least one, e.g. `php-cgi` or Python). The server must un-chunk chunked request bodies before handing them to the CGI; the CGI body end is marked by EOF when no `Content-Length` is returned. The CGI must run with its working directory set appropriately for relative-path file access. Full request + arguments must be passed via CGI env vars.
- Browser-compatible (test with real browsers, not just `curl`).
- Virtual hosts (`Host` header routing) are explicitly **out of scope** but allowed as a stretch.

## Configuration file

Inspired by NGINX's `server` block. The parser must support:
- Multiple `server` blocks, each with its own `interface:port`.
- Default error pages (overridable).
- `client_max_body_size`.
- Per-route (no regex) rules:
  - allowed HTTP methods
  - HTTP redirects
  - root directory mapping (e.g. URL `/kapouet` rooted to `/tmp/www` → `/kapouet/pouic/toto/pouet` resolves to `/tmp/www/pouic/toto/pouet`)
  - directory listing on/off (autoindex)
  - default file when the URL resolves to a directory
  - upload acceptance + upload destination
  - CGI dispatch by extension

Ship sample config(s) and default content alongside the binary so every feature can be demoed during evaluation.

## File layout & submission

Submittable files per subject: `Makefile`, `*.{h,hpp}`, `*.cpp`, `*.tpp`, `*.ipp`, configuration files. Template files (`*.tpp`/`*.ipp`) are allowed — use them if/when templates appear.

Makefile must expose: `$(NAME)`, `all`, `clean`, `fclean`, `re`. No unnecessary relinking. Current Makefile already satisfies this shape.

## README.md (subject requirement)

The current `README.md` is a stub (`tbc`). Before submission it must be **in English** and contain:
- First line, italicized, literally: *This project has been created as part of the 42 curriculum by mahkilic[, …].*
- **Description** — what the project is and its goal.
- **Instructions** — build, install, run.
- **Resources** — references (RFCs, etc.) AND a disclosure of how AI was used: which tasks, which parts of the project.

## Evaluation notes that affect code decisions

- Peer-evaluators may ask for a small, live in-defense modification (rename a behavior, tweak a data structure, add a minor feature). Favor clear module boundaries so a surgical edit is possible under time pressure.
- Bonus (cookies/sessions, multiple CGI types) is **only assessed if the mandatory part is 100% green** — don't start bonuses while anything mandatory is broken.
- The subject warns explicitly against AI-generated code the student can't explain during defense. Any non-trivial generated code should be reviewed and justifiable line-by-line.

## Quick smoke test of the current placeholder

```bash
make && ./webserv &
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc localhost 9090
```

The placeholder accepts one connection, prints the bytes, then exits — this will be replaced by the real event loop.
