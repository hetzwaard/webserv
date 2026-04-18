#include "cgi/CgiHandler.hpp"

/* ============================================================================
 * CgiHandler.cpp — IMPLEMENTATION STUB for selcyilm
 *
 * Every method below is a stub. Read CgiHandler.hpp first; it contains the
 * full design, the subject rules, and step-by-step TODO breakdowns.
 *
 * The stubs exist so the project compiles while you work. They do nothing
 * useful — start() returns false and sets FAILED, so integration will behave
 * as "CGI is not yet implemented" without crashing.
 *
 * When you finish a method, delete the TODO comment block inside it (keep the
 * high-level "what this does" comment). That makes review easy for both of us.
 *
 * Headers you'll need as you implement:
 *   #include <unistd.h>		// fork, pipe, dup2, close, read, write, chdir, _exit
 *   #include <sys/wait.h>		// waitpid, WNOHANG
 *   #include <sys/types.h>		// pid_t
 *   #include <fcntl.h>			// fcntl, O_NONBLOCK
 *   #include <signal.h>		// kill, SIGKILL
 *   #include <cstring>			// memset, strerror
 *   #include <cstdlib>			// _exit
 *
 * Allowed syscalls (from subject): fork, pipe, execve, dup2, close, read,
 * write, waitpid, kill, signal, chdir, access, stat. No others.
 * ============================================================================ */

#include <ctime>

CgiHandler::CgiHandler()
	: _state(INIT),
	  _pid(-1),
	  _stdinFd(-1),
	  _stdoutFd(-1),
	  _bodySent(0),
	  _exitStatus(-1),
	  _startTime(0) {}

CgiHandler::~CgiHandler() {
	/* TODO(selcyilm): close any still-open pipe fds, and if _pid > 0 and the
	 * child hasn't been reaped, kill(_pid, SIGKILL) + waitpid(_pid, ...) so we
	 * don't leak zombies. Called whether the handler finished cleanly or not. */
}

bool	CgiHandler::start(const std::string& /*interpreter*/,
						const std::string& /*scriptPath*/,
						const EnvMap& /*env*/,
						const std::string& /*requestBody*/) {
	/* TODO(selcyilm): the big one. See CgiHandler.hpp for the full step list.
	 * High-level skeleton:
	 *
	 *   _startTime = std::time(NULL);
	 *
	 *   int toChild[2], fromChild[2];
	 *   if (pipe(toChild)  < 0) goto fail;
	 *   if (pipe(fromChild)< 0) goto fail;
	 *
	 *   _pid = fork();
	 *   if (_pid < 0) goto fail;
	 *
	 *   if (_pid == 0) {
	 *       // CHILD
	 *       dup2(toChild[0],  STDIN_FILENO);
	 *       dup2(fromChild[1], STDOUT_FILENO);
	 *       close(toChild[0]); close(toChild[1]);
	 *       close(fromChild[0]); close(fromChild[1]);
	 *       chdir(<dir of scriptPath>);
	 *       execve(interpreter, argv, envp);
	 *       _exit(1);   // NEVER return from child; no throw, no cout
	 *   }
	 *
	 *   // PARENT
	 *   close(toChild[0]);        // parent doesn't read from toChild
	 *   close(fromChild[1]);      // parent doesn't write to fromChild
	 *   _stdinFd  = toChild[1];
	 *   _stdoutFd = fromChild[0];
	 *   fcntl(_stdinFd,  F_SETFL, O_NONBLOCK);
	 *   fcntl(_stdoutFd, F_SETFL, O_NONBLOCK);
	 *   _requestBody = requestBody;
	 *   _bodySent = 0;
	 *   _state = _requestBody.empty() ? READING_STDOUT : SENDING_STDIN;
	 *   return true;
	 *
	 * fail:
	 *   close everything still open;
	 *   _state = FAILED;
	 *   _rawOutput = "Status: 500 Internal Server Error\r\n\r\nCGI spawn failed";
	 *   return false;
	 */
	_state = FAILED;
	_rawOutput = "Status: 500 Internal Server Error\r\n"
				"Content-Type: text/plain\r\n\r\n"
				"CGI not implemented yet.\n";
	return false;
}

void	CgiHandler::onStdinWritable() {
	/* TODO(selcyilm): write bytes from _requestBody+_bodySent. Advance
	 * _bodySent. When _bodySent == _requestBody.size(): close(_stdinFd);
	 * _stdinFd = -1; _state = READING_STDOUT;
	 * Server will observe _stdinFd == -1 and unregister it from poll.
	 * On write returning <= 0: _state = FAILED.  NEVER check errno. */
}

void	CgiHandler::onStdoutReadable() {
	/* TODO(selcyilm): read into a local buffer, append to _rawOutput.
	 * read == 0 → child closed stdout → close(_stdoutFd); _stdoutFd = -1;
	 * then call tick() to attempt reap; if already reaped, _state = DONE.
	 * read < 0 → _state = FAILED. NEVER check errno. */
}

void	CgiHandler::tick() {
	/* TODO(selcyilm):
	 * 1. Timeout check:
	 *      if (_state in running-states) && (now - _startTime > CGI_TIMEOUT_SECONDS)
	 *          kill(_pid, SIGKILL);
	 * 2. Non-blocking reap:
	 *      int status;
	 *      pid_t r = waitpid(_pid, &status, WNOHANG);
	 *      if (r == _pid) {
	 *          _exitStatus = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	 *          _pid = -1;
	 *          if (_stdoutFd == -1) _state = DONE;  // both conditions met
	 *      }
	 * Note: _state can only flip to DONE when BOTH the child has been reaped
	 * AND stdout has hit EOF. Either one alone is not enough.
	 */
}

/* ---- accessors: these are DONE, you don't need to change them ---- */

CgiHandler::State		CgiHandler::state() const		{ return _state; }
bool					CgiHandler::isDone() const		{ return _state == DONE; }
bool					CgiHandler::isFailed() const	{ return _state == FAILED; }
int						CgiHandler::stdinFd() const		{ return _stdinFd; }
int						CgiHandler::stdoutFd() const	{ return _stdoutFd; }
const std::string&		CgiHandler::rawOutput() const	{ return _rawOutput; }
int						CgiHandler::exitStatus() const	{ return _exitStatus; }
