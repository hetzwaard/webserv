#include "../include/Cgi.hpp"

static std::string	toStrCgi(size_t n)
{
	std::ostringstream	oss;
	oss << n;
	return (oss.str());
}

std::string	executeCgi(const Request &req, const Location *loc, const std::string &fsPath)
{
	int	outPipe[2];
	int	inPipe[2];
	if (pipe(outPipe) < 0)
		return ("");
	if (pipe(inPipe) < 0)
	{
		close(outPipe[0]);
		close(outPipe[1]);
		return ("");
	}

	// Building the CGI env as KEY=value strs (similar to minishell)

	std::vector<std::string>	env;
	env.push_back("GATEWAY_INTERFACE=CGI/1.1");
	env.push_back("SERVER_PROTOCOL=HTTP/1.1");
	env.push_back("REQUEST_METHOD=" + req.method);
	env.push_back("QUERY_STRING=" + req.query);
	env.push_back("SCRIPT_NAME=" + fsPath);
	env.push_back("PATH_INFO=" + fsPath);
	env.push_back("CONTENT_LENGTH=" + toStrCgi(req.body.size()));
	env.push_back("CONTENT_TYPE=application/x-www-form-urlencoded");

	// Convert to the char* array execve wants:

	std::vector<char *>	envp;
	for (size_t i = 0; i < env.size(); ++i)
		envp.push_back(const_cast<char *>(env[i].c_str()));
	envp.push_back(NULL);

	pid_t	pid = fork();
	if (pid < 0)
		return ("");

	if (pid == 0)
	{
		dup2(inPipe[0], STDIN_FILENO); // my stdin is now the input pipe's read end
		dup2(outPipe[1], STDOUT_FILENO); // my stdout is the output pipe's write end
		close(inPipe[0]);
		close(inPipe[1]);
		close(outPipe[0]);
		close(outPipe[1]);

		char	*argv[3];
		argv[0] = const_cast<char *>(loc->cgiBin.c_str());
		argv[1] = const_cast<char *>(fsPath.c_str());
		argv[2] = NULL;

		execve(loc->cgiBin.c_str(), argv, &envp[0]);
		_exit(1);
	}

	// PARENT
	close(inPipe[0]); // parent doesn't read the input pipe
	close(outPipe[1]); // parent doesn't write the output pipe

	if (!req.body.empty())
		write(inPipe[1], req.body.c_str(), req.body.size());
	close(inPipe[1]); // done writing — child's stdin now hits EOF

	std::string	output;
	char			buf[4096];
	ssize_t		n;

	while ((n = read(outPipe[0], buf, sizeof(buf))) > 0)
		output.append(buf, n);
	close(outPipe[0]);

	waitpid(pid, NULL, 0);

	return (output);
}
