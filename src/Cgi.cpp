#include "../include/Cgi.hpp"
#include "../include/Server.hpp"

static std::string	toStrCgi(size_t n)
{
	std::ostringstream	oss;
	oss << n;
	return (oss.str());
}

static bool	writeBodyToFile(const std::string &body, int &outFd)
{
	static size_t	counter = 0;

	std::string	path = "/tmp/webserv_cgi_" + toStrCgi(counter++);

	int	fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return (false);

	size_t	sent = 0;
	while (sent < body.size())
	{
		ssize_t	n = write(fd, body.c_str() + sent, body.size() - sent);
		if (n <= 0)
		{
			close(fd);
			std::remove(path.c_str());
			return (false);
		}
		sent += n;
	}
	close(fd);

	outFd = open(path.c_str(), O_RDONLY);	// reopen, offset starts at 0, no lseek needed
	std::remove(path.c_str());					// path gone, the open fd keeps the data alive

	return (outFd >= 0);
}

bool	startCgi(const Request &req, const std::string &cgiBin, const std::string &fsPath,
	pid_t &outPid, int &outFd)
{
	int	outPipe[2];
	int	bodyFd;

	if (pipe(outPipe) < 0)
		return (false);
	if (!writeBodyToFile(req.body, bodyFd))
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
	env.push_back("HTTP_COOKIE=" + req.cookie);
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
		close(bodyFd);
		return (false);
	}

	if (pid == 0)
	{
		dup2(bodyFd, STDIN_FILENO);
		dup2(outPipe[1], STDOUT_FILENO);
		close(bodyFd);
		close(outPipe[0]); close(outPipe[1]);

		if (chdir(dir.c_str()) != 0)
			std::exit(1);

		char	*argv[3];
		argv[0] = const_cast<char *>(cgiBin.c_str());
		argv[1] = const_cast<char *>(file.c_str());
		argv[2] = NULL;

		execve(cgiBin.c_str(), argv, &envp[0]);
		std::exit(1);
	}

	close(outPipe[1]);
	close(bodyFd);

	fcntl(outPipe[0], F_SETFL, O_NONBLOCK);

	outPid = pid;
	outFd = outPipe[0];
	return (true);
}
