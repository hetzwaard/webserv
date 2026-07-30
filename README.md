*This project has been created as part of the 42 curriculum by mahkilic, selcyilm.*

# WEBSERV 🌐

### A tiny HTTP/1.1 server in C++98

Webserv is a small HTTP server written in C++98 for the 42 curriculum (team project). It listens on one or more ports, reads HTTP requests, and answers them — serving static files, accepting uploads, running CGI scripts, and following the rules of a configuration file.

The goal: understand how an HTTP server really works — sockets, `poll()`, and the request/response cycle — while keeping the code readable and safe.

---
## Features
- Single non-blocking `poll()` loop for **all** I/O (listeners, clients, and CGI pipes)
- Configuration file with `server` blocks and per-route `location` blocks
- Methods:
  - `GET` (static files, directory listing)
  - `POST` (file upload)
  - `DELETE` (remove a file)
- Multiple servers on different ports, each with its own config
- Per-route rules:
  - Accepted HTTP methods
  - Root directory + default index file
  - HTTP redirection (`return`)
  - Directory listing on/off (`autoindex`)
  - Upload storage location
  - CGI by file extension
- CGI execution for both `GET` and `POST`
- Custom error pages (falls back to a generated page if the file is missing)
- Client body size limit (`client_max_body_size` -> `413`)
- Accurate HTTP status codes
- Request, CGI, and idle-client timeouts — nothing hangs forever
- `SIGPIPE` ignored so a disconnecting client can't kill the server

### Bonus
- **Multiple CGI types** — different interpreters chosen by file extension (e.g. `.py` via python3, `.sh` via bash), configured per location
- **Cookies and sessions** — the server passes the incoming `Cookie` header to CGI as `HTTP_COOKIE` and forwards the script's `Set-Cookie` back to the client; a visit-counter demo lives at `/session.py`

---
## Architecture Overview
```
include/         Public headers (Config, Server, Http, Cgi)
src/
  main.cpp       Entry point: read config arg, build servers, run
  Config.cpp     Tokenizer + recursive-descent config parser
  Server.cpp     The poll() loop: accept, read, write, timeouts, CGI dispatch
  Http.cpp       Request parsing, static serving, methods, status codes
  Cgi.cpp        fork / execve / pipe execution of CGI scripts
default.conf     Example config (two servers, several routes)
www/  www2/       Test websites + demo CGI scripts
uploads/         Upload storage (kept in the repo via .gitkeep)
```
Flow:
1. Parse the config file into `server` / `location` structs
2. Open a listening socket per server, all added to one `poll()` set
3. `poll()` reports ready fds — accept, read, or write, one operation each
4. A complete request is parsed and routed to static / upload / delete / CGI
5. The response is buffered and written back when the socket is writable
6. Loop forever; sweep timed-out CGIs and idle clients each cycle

---
## Key Modules
- **Config**: turns config text into tokens, then walks them into `ServerConfig` / `Location` structs (parser mirrors the file's nesting)
- **Server**: owns the single `poll()` array and three fd maps (listeners, clients, CGI pipes); does no HTTP parsing itself
- **Http**: request parsing, path resolution, static files, error pages, redirects, autoindex, cookie extraction — no socket code
- **Cgi**: forks a child, wires two pipes (body in, output out), `execve`s the interpreter chosen for the request's extension, runs it in the script's own directory
- **Timeouts**: hung CGIs are killed (`504`), idle clients are dropped

---
## Design Notes
One `poll()` call watches every fd — listeners, clients, and CGI output pipes — for read and write at the same time. Each client is read or written **at most once per loop iteration**. A `recv`/`send`/`read` returning `0` or less closes the connection, and `errno` is never checked after a socket operation.

CGI runs in a forked child whose stdout is piped back into the same `poll()` set, so a slow or hung script never blocks other clients — scripts that run past the timeout are killed and answered with `504 Gateway Timeout`.

---
## Error Handling
We try to fail early and clearly.

Examples:
```
$ ./webserv nope.conf
Error: cannot open config file: nope.conf

$ ./webserv                # duplicate port in config
Error: bind() failed on port 4242

$ curl -i localhost:4242/missing
HTTP/1.1 404 Not Found

$ curl -i -X DELETE localhost:4242/                # method not allowed here
HTTP/1.1 405 Method Not Allowed
```
Rules of thumb:
- Config errors throw with a clear message and the server refuses to start
- Every response goes through one builder, so headers are consistent
- The server never crashes on bad input — malformed requests get a status code, not a segfault

---
## Configuration
A config has one or more `server` blocks, each with one or more `location` routes:
```
server {
    host 0.0.0.0;
    port 4242;
    client_max_body_size 1000000;
    error_page 404 ./www/404.html;

    location / {
        root ./www;
        index index.html;
        methods GET POST DELETE;
        autoindex off;
        upload_dir ./uploads;
        cgi .py /usr/bin/python3;    # extension -> interpreter
        cgi .sh /bin/bash;           # a second CGI type
    }

    location /old {
        methods GET;
        return /;                    # 301 redirect
    }
}
```
Each `cgi <ext> <interpreter>;` line adds one CGI type, so a location can run
several. Longest-prefix matching decides which `location` serves a request
(`/uploads/x` prefers a `/uploads` block over `/`).

---
## Building
Prerequisites:
- `make`, a C++98 compiler (`c++`)
- Python 3 and Bash for the CGI demos (paths set by the `cgi` lines in the config)

Clone it:
```bash
git clone https://github.com/hetzwaard/webserv && cd webserv
```
Build:
```bash
make
```
Rebuild from scratch:
```bash
make re
```
Clean objects / full cleanup:
```bash
make clean
make fclean
```

---
## Running
```bash
./webserv [configuration file]
```
If no file is given, `default.conf` is used.

Examples:
```bash
# static file
curl -i localhost:4242/

# upload a file, then get it back
curl -X POST --data-binary @file.txt localhost:4242/uploads/file.txt
curl -i localhost:4242/uploads/file.txt

# delete it
curl -i -X DELETE localhost:4242/uploads/file.txt

# CGI — two interpreters, chosen by extension (bonus)
curl -i localhost:4242/hello.py       # python3
curl -i localhost:4242/hello.sh       # bash
curl -i -X POST -d "name=world" localhost:4242/hello.py

# cookies / sessions (bonus): the count climbs across requests
curl -i -c jar localhost:4242/session.py    # first visit: Set-Cookie, count 1
curl -i -b jar localhost:4242/session.py    # returning: count 2

# a redirect
curl -iL localhost:4242/old

# the second server on another port
curl -i localhost:4243/
```
You can also open `http://localhost:4242/` in a browser and click around —
including the upload form at `/form.html`, the directory listing at `/uploads/`,
and the session counter at `/session.py` (reload to watch it climb; try an
incognito window for a fresh session).

---
## Resources
Classic references we used while building this:
- **RFC 7230** and **RFC 2616** — HTTP/1.1 message syntax and semantics
- **RFC 3875** — the CGI/1.1 specification
- **NGINX** configuration documentation — inspiration for the config file structure
- `man` pages: `poll`, `socket`, `bind`, `listen`, `accept`, `fork`, `execve`, `pipe`, `dup2`, `waitpid`, `chdir`
- Testing tools: `curl`, `telnet`, `siege`, `cat -A`, `ps`

### Use of AI
We used Claude Code as a learning and planning tool, specifically:

- **Architecture and explanation.** Claude helped us plan the overall design — the `Config` / `Server` / `Http` / `Cgi` file split and the single-`poll()`-loop model where listeners, clients, and CGI pipes all live in one fd set. It also walked us through the syscall-level mechanics layer by layer: the socket lifecycle (`socket`/`bind`/`listen`/`accept`), how `poll()` drives non-blocking I/O, and the `fork`/`execve`/`pipe`/`dup2`/`waitpid` chain behind CGI, including the CGI environment-variable protocol.

- **Typed by hand, built in layers.** We typed every line ourselves rather than pasting, and built the server one testable layer at a time — sockets, then config, then HTTP, then uploads/delete, then CGI, then hardening, then the bonuses — compiling and testing each layer before moving on. This kept us able to explain and defend every part.

- **Debugging was ours.** When something broke, we diagnosed it ourselves by reading the actual bytes and processes: `curl -i` to see raw responses, `cat -A` to spot bad `\r\n` and header typos, and `ps` to confirm no CGI processes were left behind. Most bugs (inverted booleans, a missing blank line before the body, an unsigned `recv` return) we found this way rather than being told the answer.

- **Split of work.** mahkilic focused on the socket layer, the `poll()` loop, request handling, and CGI; selcyilm focused on the configuration file tokenizer and parser. We built against a shared `ServerConfig` struct so the two halves could progress in parallel and merge cleanly.

---
Built as a 42 project by **mahkilic** and **selcyilm**. 🌐