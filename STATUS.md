# webserv — Project Status

*Last updated: 2026-04-19*

## Team

| Person | Login | Responsibility |
|---|---|---|
| A | mahkilic | Poll loop + incremental HTTP parse |
| B | selcyilm | CGI (fork/pipe/execve + non-blocking pipe I/O) |
| Shared | both | Config parser, routing, error pages, README |

## What's done

- [x] Non-blocking poll() event loop — single loop, all fds
- [x] Incremental HTTP/1.1 request parser (request line, headers, Content-Length, chunked)
- [x] HTTP response builder (status codes, reason phrases, headers)
- [x] Keep-alive (HTTP/1.1 default, connection reuse)
- [x] Static file serving (GET) with MIME type detection
- [x] Directory listing (autoindex)
- [x] File upload (POST /uploads/)
- [x] File deletion (DELETE /uploads/)
- [x] Path traversal protection
- [x] Redirects (301/302 — hardcoded for demo, needs config)
- [x] Error responses (400, 403, 404, 405, 413, 501)
- [x] Signal handling (SIGINT/SIGTERM graceful, SIGPIPE ignored)
- [x] Idle timeout (30s)
- [x] Glassmorphism dashboard page (www/index.html)
- [x] SO_REUSEADDR on listen sockets

## What's left

### Priority 1 — Config parser (BLOCKER)
Almost everything below depends on this. Needs to produce structs like:
```cpp
struct LocationConfig {
    std::string path;               // "/uploads"
    std::vector<std::string> allowed_methods;
    std::string root;
    std::string index;
    bool autoindex;
    std::string upload_dir;
    std::string redirect;
    int redirect_code;
    std::map<std::string, std::string> cgi; // ".py" -> "/usr/bin/python3"
};

struct ServerConfig {
    std::string host;
    int port;
    size_t client_max_body_size;
    std::map<int, std::string> error_pages; // 404 -> "/errors/404.html"
    std::vector<LocationConfig> locations;
};
```

### Priority 2 — CGI (selcyilm)
- [ ] CgiHandler class: fork + pipe + execve
- [ ] CGI pipe fds added to Server's poll vector (NOT a separate poll loop)
- [ ] CGI environment variables (REQUEST_METHOD, QUERY_STRING, CONTENT_TYPE, etc.)
- [ ] Un-chunked body passed to CGI stdin (parser already decodes chunked)
- [ ] CGI timeout + kill for hanging scripts
- [ ] Error handling (execve failure, broken pipe, bad output)

### Priority 3 — Config-dependent features
- [ ] Config file parsing (NGINX-style server blocks)
- [ ] Multiple server blocks with different ports
- [ ] Per-route method restrictions (allowed_methods)
- [ ] Custom error pages from disk
- [ ] client_max_body_size from config (currently hardcoded 1MB)
- [ ] Root directory mapping per route
- [ ] Default index file per route
- [ ] Upload directory per route
- [ ] Redirect routes from config (currently hardcoded /old-page, /redirect-me)

### Priority 4 — Before submission
- [ ] README.md (required format — see subject)
- [ ] Sample config files in configs/
- [ ] Test CGI scripts in cgi-bin/
- [ ] Recreate www/forbidden.txt with `chmod 000` for 403 demo
- [ ] Stress test with siege (>99.5% availability)
- [ ] Memory leak check (valgrind)

## Bonuses (only if mandatory is 100%)
- [ ] Multiple CGI interpreters (easy — just config mapping)
- [ ] Cookies & sessions (medium — ~100-150 lines)

## Architecture

```
src/
├── main.cpp              # Entry point — creates Server, adds listeners, runs
├── server/
│   ├── Server.cpp        # Poll loop, accept, read/write dispatch, file serving
│   └── Connection.cpp    # Per-client state (fd, buffers, request, keep-alive)
├── http/
│   ├── HttpRequest.cpp   # Incremental parser (state machine)
│   └── HttpResponse.cpp  # Response builder + serializer
└── cgi/
    └── CgiHandler.cpp    # (skeleton — selcyilm's part)

include/                  # Matching .hpp headers
www/                      # Document root (static files)
www/uploads/              # Upload directory (auto-created)
configs/                  # Config files (to be created)
```

## Key eval gotchas
- **Grade 0 if:** crash, errno check after read/write, read/write without poll, blocking I/O on sockets, multiple poll loops
- **Siege test:** availability must be >99.5% — keep-alive helps
- **Memory leaks:** any leak = flag
- **Live modification:** evaluators may ask for a small code change during defense
