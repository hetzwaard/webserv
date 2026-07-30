#include "../include/Cgi.hpp"
#include "../include/Server.hpp"

static std::string	toStrCgi(size_t n)
{
	std::ostringstream	oss;
	oss << n;
	return (oss.str());
}

bool	startCgi(const Request &req, const std::string &cgiBin, const std::string &fsPath,
	pid_t &outPid, int &outFd)
{
	int	outPipe[2];
	int	inPipe[2];

	if (pipe(outPipe) < 0)
		return (false);
	if (pipe(inPipe) < 0)
	{
		close(outPipe[0]);
		close(outPipe[1]);
		return (false);
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

	std::string					dir = ".";
	std::string					file = fsPath;
	std::string::size_type	slash = fsPath.rfind('/');
	if (slash != std::string::npos)
	{
		dir = fsPath.substr(0, slash);
		file = fsPath.substr(slash + 1);
	}

	pid_t	pid = fork();
	if (pid < 0)
	{
		close(outPipe[0]); close(outPipe[1]);
		close(inPipe[0]); close(inPipe[1]);
		return (false);
	}

	if (pid == 0)
	{
		dup2(inPipe[0], STDIN_FILENO);
		dup2(outPipe[1], STDOUT_FILENO);
		close(inPipe[0]); close(inPipe[1]);
		close(outPipe[0]); close(outPipe[1]);

		if (chdir(dir.c_str()) != 0)
			_exit(1);

		char	*argv[3];
		argv[0] = const_cast<char *>(cgiBin.c_str());
		argv[1] = const_cast<char *>(file.c_str());
		argv[2] = NULL;

		execve(cgiBin.c_str(), argv, &envp[0]);
		_exit(1);
	}

	close(inPipe[0]);
	close(outPipe[1]);

	if (!req.body.empty())
		write(inPipe[1], req.body.c_str(), req.body.size());
	close(inPipe[1]);

	fcntl(outPipe[0], F_SETFL, O_NONBLOCK);

	outPid = pid;
	outFd = outPipe[0];
	return (true);
}
