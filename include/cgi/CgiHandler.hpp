#ifndef CGIHANDLER_HPP
# define CGIHANDLER_HPP

/* ============================================================================
 * CgiHandler — runs one CGI request
 * ============================================================================
 *
 * OWNER: selcyilm
 * READER: this file is a tutorial + reference. Read it top to bottom before
 *         writing any code. Every TODO marks a decision YOU must be able to
 *         defend at peer-evaluation — don't let AI write it for you.
 *
 * ----------------------------------------------------------------------------
 * WHAT IS CGI?
 * ----------------------------------------------------------------------------
 * CGI ("Common Gateway Interface", RFC 3875) is a protocol for a web server
 * to run an external program to produce a response.
 *
 * When a request like  GET /cgi-bin/hello.py?name=sedat  arrives and the
 * router decides it's CGI:
 *
 *   1. The server fork()s.
 *   2. The child sets up stdin/stdout to pipes, sets env vars describing the
 *      request, chdir()s to the script's directory, and execve()s the
 *      interpreter (e.g. /usr/bin/python3) with the script as argv[1].
 *   3. The parent writes the request BODY to the child's stdin pipe,
 *      then reads the child's stdout pipe for the HTTP response.
 *   4. When the child exits, parent waitpid()s it and turns the collected
 *      output into an HTTP response for the client.
 *
 * The CGI program itself speaks a simple format on its stdout:
 *
 *     Content-Type: text/html\r\n
 *     Status: 200 OK\r\n         <-- optional; default 200
 *     \r\n
 *     <html>...body...</html>
 *
 * Headers, blank line, body. Just like HTTP without the request line.
 *
 * ----------------------------------------------------------------------------
 * SUBJECT RULES THAT APPLY HERE (grade-0 if broken)
 * ----------------------------------------------------------------------------
 *   - fork() is allowed ONLY for CGI. This class is the only place in the
 *     codebase that may call fork().
 *   - Both pipe fds (child_stdin, child_stdout) MUST be non-blocking and MUST
 *     be registered in the Server's single poll() loop. You never read/write
 *     them without poll saying "ready". Never check errno after read/write.
 *   - waitpid() MUST use WNOHANG. A blocking waitpid would freeze the whole
 *     server.
 *   - Chunked request bodies must be UN-CHUNKED before being written to the
 *     child's stdin. The HTTP parser does this; you receive an already-decoded
 *     body in `requestBody`.
 *   - If the CGI's output has no Content-Length, the body ends at EOF on the
 *     stdout pipe. Treat that as a valid termination, not an error.
 *   - The CGI child must run with its working directory set to the script's
 *     directory, so relative file access inside the script works.
 *   - Allowed syscalls for this module: fork, pipe, execve, dup2, close, read,
 *     write, waitpid, kill, signal, chdir, access, stat. That's it.
 *
 * ----------------------------------------------------------------------------
 * LIFECYCLE / STATE MACHINE
 * ----------------------------------------------------------------------------
 *
 *   INIT ──start()──► SPAWNED ──► SENDING_STDIN ──► READING_STDOUT ──► DONE
 *                        │            │                   │
 *                        ▼            ▼                   ▼
 *                      FAILED      FAILED              FAILED
 *
 *   INIT            Constructed, no process yet.
 *   SPAWNED         fork() + execve() succeeded. Pipes are open and
 *                   non-blocking. Parent registers both pipe fds with the
 *                   Server's poll set.
 *   SENDING_STDIN   Writing the request body into the child's stdin pipe
 *                   (bytes_sent offset pattern, same as Connection). When all
 *                   body bytes written, close stdin to send EOF to child.
 *   READING_STDOUT  Reading child's stdout into `rawOutput`. recv==0 means
 *                   child closed stdout → EOF → transition to waitpid.
 *   DONE            waitpid(WNOHANG) reaped the child. `rawOutput` contains
 *                   the full CGI response, ready to parse into HttpResponse.
 *   FAILED          Any unrecoverable error (fork failed, exec failed,
 *                   timeout, killed). `rawOutput` holds a default 500-body.
 *
 * ----------------------------------------------------------------------------
 * HOW THIS INTEGRATES WITH Server (what you'll wire up LATER)
 * ----------------------------------------------------------------------------
 *
 * You do NOT need to modify Server.cpp/hpp yet. That's a separate integration
 * step we'll do together. But here's what it will look like so you know the
 * shape of the thing you're building:
 *
 *   1. Server will gain `std::map<int, CgiHandler*> _cgiByFd;` keyed by pipe
 *      fd. Both stdin and stdout pipe fds point to the same CgiHandler.
 *   2. When the router decides a request is CGI:
 *         CgiHandler* cgi = new CgiHandler(...);
 *         if (cgi->start(...)) {
 *             _cgiByFd[cgi->stdinFd()]  = cgi;
 *             _cgiByFd[cgi->stdoutFd()] = cgi;
 *             server.addFd(cgi->stdinFd(),  POLLOUT);
 *             server.addFd(cgi->stdoutFd(), POLLIN);
 *         }
 *   3. In Server::run(), after the existing client/listener dispatch, if the
 *      fd is in _cgiByFd, call cgi->onStdinWritable() or cgi->onStdoutReadable().
 *   4. Every loop, call cgi->tick() on each active CGI — it calls waitpid
 *      (WNOHANG) and flips to DONE when the child exits.
 *   5. When cgi->isDone() or isFailed(), parse rawOutput() into an
 *      HttpResponse on the owning Connection, flip the Connection to
 *      WRITING_RESPONSE, unregister & close the pipe fds, delete the handler.
 *
 * For now, get this class working standalone with a simple test driver (see
 * cgi-bin/hello.py and a main_cgi_test.cpp you may write to isolate it).
 *
 * ============================================================================ */

# include <string>
# include <vector>
# include <map>
# include <ctime>
# include <sys/types.h>		// pid_t

class CgiHandler {
public:
	enum State {
		INIT,
		SPAWNED,
		SENDING_STDIN,
		READING_STDOUT,
		DONE,
		FAILED
	};

	/* The environment map is populated by the CALLER (the router) before
	 * start() is called. Typical keys (CGI/1.1):
	 *   REQUEST_METHOD       "GET" / "POST" / "DELETE"
	 *   QUERY_STRING         "name=sedat&x=1"  (the part after '?')
	 *   CONTENT_LENGTH       decimal string of body size; empty for GET
	 *   CONTENT_TYPE         from request Content-Type header
	 *   SCRIPT_NAME          "/cgi-bin/hello.py"
	 *   SCRIPT_FILENAME      "/abs/path/to/cgi-bin/hello.py"
	 *   PATH_INFO            extra path after the script (often empty)
	 *   SERVER_PROTOCOL      "HTTP/1.1"
	 *   SERVER_NAME          host name from config or Host header
	 *   SERVER_PORT          "9090"
	 *   SERVER_SOFTWARE      "webserv/0.1"
	 *   GATEWAY_INTERFACE    "CGI/1.1"
	 *   REMOTE_ADDR          client IP
	 *   HTTP_*               every request header, upper-cased with dashes->_
	 *                        e.g. User-Agent -> HTTP_USER_AGENT
	 *
	 * Responsibility: the ROUTER builds this map; CgiHandler just converts it
	 * to the execve envp format and passes it through.
	 */
	typedef std::map<std::string, std::string> EnvMap;

	CgiHandler();
	~CgiHandler();

	/* Launch the CGI.
	 *   interpreter   absolute path to interpreter, e.g. "/usr/bin/python3"
	 *                 (for php-cgi, use the path to php-cgi itself)
	 *   scriptPath    absolute path to the script, e.g. "/.../cgi-bin/hello.py"
	 *   env           env vars to pass (see EnvMap comment above)
	 *   requestBody   already-decoded body (chunked already un-chunked); may be empty
	 *
	 * Returns true on successful fork+exec setup, false on FAILED. On failure,
	 * state() == FAILED and rawOutput() contains a canned error body.
	 *
	 * TODO(selcyilm): implement. Steps:
	 *   1. pipe(toChild);     // parent writes, child reads as stdin
	 *   2. pipe(fromChild);   // child writes as stdout, parent reads
	 *   3. fork().
	 *      CHILD:
	 *        - dup2(toChild[0], STDIN_FILENO);
	 *        - dup2(fromChild[1], STDOUT_FILENO);
	 *        - close all four original pipe fds
	 *        - chdir(dirname(scriptPath))
	 *        - build argv = { interpreter, scriptPath, NULL }
	 *        - build envp from env map (vector<std::string> + vector<char*>)
	 *        - execve(interpreter, argv, envp);
	 *        - if we return: _exit(1)   // NEVER return from child; never throw
	 *      PARENT:
	 *        - close the fds the child owns (toChild[0], fromChild[1])
	 *        - set toChild[1] and fromChild[0] non-blocking (fcntl O_NONBLOCK)
	 *        - store pid, _stdinFd, _stdoutFd
	 *        - state = SPAWNED (or SENDING_STDIN if requestBody non-empty)
	 */
	bool	start(const std::string& interpreter,
				const std::string& scriptPath,
				const EnvMap& env,
				const std::string& requestBody);

	/* Server calls this when poll reports stdinFd() writable.
	 * TODO(selcyilm): write as many body bytes as possible from
	 * (_requestBody.data() + _bodySent) onward. Advance _bodySent. When all
	 * bytes sent, close stdin (this signals EOF to child) and transition to
	 * READING_STDOUT. Remove stdinFd from poll. On write <= 0 → FAILED.
	 */
	void	onStdinWritable();

	/* Server calls this when poll reports stdoutFd() readable.
	 * TODO(selcyilm): recv/read into a local buffer, append to _rawOutput.
	 * read==0 → child closed stdout (EOF) → call reap() → state becomes DONE
	 * (or FAILED if child exited non-zero). read<0 → FAILED.
	 */
	void	onStdoutReadable();

	/* Called once per main-loop iteration. Non-blocking waitpid.
	 * TODO(selcyilm): if pid > 0 and state in {SPAWNED, SENDING_STDIN,
	 * READING_STDOUT}, call waitpid(pid, &status, WNOHANG). If it returned >0,
	 * record exit status and, if stdout already hit EOF, transition to DONE.
	 * Also enforce a timeout (e.g. 10 seconds) — if exceeded, kill(pid,SIGKILL)
	 * and set state = FAILED.
	 */
	void	tick();

	/* Accessors the Server and the response builder will use. */
	State				state() const;
	bool				isDone() const;
	bool				isFailed() const;
	int					stdinFd() const;
	int					stdoutFd() const;
	const std::string&	rawOutput() const;
	int					exitStatus() const;

private:
	CgiHandler(const CgiHandler&);
	CgiHandler& operator=(const CgiHandler&);

	/* Helpers — also TODO for you.
	 * - buildArgv / buildEnvp: turn std::vector<std::string> owned by this
	 *   object into the argv/envp char* arrays execve wants.
	 * - cleanup: close any still-open fds, kill pid if still running.
	 */

	State		_state;
	pid_t		_pid;
	int			_stdinFd;		// parent writes here; -1 when closed
	int			_stdoutFd;		// parent reads here; -1 when closed
	std::string	_requestBody;
	std::size_t	_bodySent;
	std::string	_rawOutput;
	int			_exitStatus;
	std::time_t	_startTime;

	static const int	CGI_TIMEOUT_SECONDS = 10;
};

#endif
