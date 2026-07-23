*This project has been created as part of the 42 curriculum by mahkilic, selcyilm.*

# webserv

## Description

An HTTP/1.1 server written in C++98. It serves static files, handles
file uploads and deletion, executes CGI scripts, and supports multiple
virtual servers on different ports — all driven by a single non-blocking
`poll()` loop.

Features:
- Configuration file with server blocks and per-route location blocks
- GET, POST (upload), DELETE
- CGI execution by file extension (Python), for GET and POST
- Custom error pages, directory listing, HTTP redirection
- Client body size limits, request and CGI timeouts

## Instructions

	make
	./webserv default.conf

The server reads `default.conf` if no argument is given. Requires a
Python 3 interpreter at the path set by `cgi_bin` for CGI routes.

## Technical choices

Single `poll()` loop over one fd array containing listening sockets,
client sockets, and CGI output pipes. Each client is read or written at
most once per cycle; a return value of 0 or less closes the connection
without inspecting `errno`. CGI runs in a forked child with its stdout
piped back into the same poll set, so a slow script never blocks other
clients; scripts exceeding the timeout are killed and answered with 504.

## Resources

- RFC 7230 / RFC 2616 — HTTP message syntax and semantics
- RFC 3875 — The CGI/1.1 specification
- NGINX configuration documentation (config file structure)
- `man` pages: poll, socket, bind, listen, accept, fork, execve, pipe, dup2, waitpid

### Use of AI

We've used Claude Code to plan the architecture and to explain the syscall-level mechanics layer by layer, tested each layer before moving to the next, and debugged the failures as they came.
