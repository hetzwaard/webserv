#include "../include/Server.hpp"
#include "../include/Http.hpp"
#include "../include/Cgi.hpp"

Server::Server(const std::vector<ServerConfig> &servers) : _servers(servers) {}

Server::~Server() {}

void	Server::addPollFd(int fd, short events)
{
	struct pollfd	pfd;

	pfd.fd = fd;				// which socket?
	pfd.events = events;		// what am I waiting for?
	pfd.revents = 0;			// poll fills this in with what actually happened
	_pfds.push_back(pfd);
}

void	Server::setupListeners()
{
	for (size_t i = 0; i < _servers.size(); ++i)
	{
		int	fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0)
			throw std::runtime_error("socket() failed");

		int	opt = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));	// saving sanity level.. (lemme grap it again)

		struct sockaddr_in	addr;	// to know where to socket lives
		std::memset(&addr, 0, sizeof(addr));	// to zero first so no garbage bytes linger in the struct's padding
		addr.sin_family = AF_INET;	// IPv4 again
		addr.sin_addr.s_addr = inet_addr(_servers[i].host.c_str());	// converting 0.0.0.0 to kernel language
		addr.sin_port = htons(_servers[i].port);	// flips the bytes (safer in 16-bit numeric)
		
		if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		{
			std::ostringstream	msg;
			msg << "bind() failed on port " << _servers[i].port;
			throw std::runtime_error(msg.str());
		}

		if (listen(fd, SOMAXCONN) < 0)	// the socket now is waiting queue
			throw std::runtime_error("listen() failed");

		fcntl(fd, F_SETFL, O_NONBLOCK);	// non-blocking socket method

		addPollFd(fd, POLLIN);	// tell me when someone knocks on
		_listeners[fd] = &_servers[i];	// this fd is one I accept on, and here's the config behind it

		std::cout << "Currently listening on " << _servers[i].host << ":" << _servers[i].port << std::endl;
	}
}

void	Server::checkCgiTimeouts()
{
	time_t	now = time(NULL);

	std::map<int, CgiProcess>::iterator	it = _cgi.begin();
	while (it != _cgi.end())
	{
		if (now - it->second.start < CGI_TIMEOUT)
		{
			++it;
			continue ;
		}

		int	pipeFd = it->first;
		int	clientFd = it->second.clientFd;

		kill(it->second.pid, SIGKILL);
		waitpid(it->second.pid, NULL, 0);

		close(pipeFd);
		removePollFd(pipeFd);

		std::map<int, CgiProcess>::iterator	dead = it;
		++it;
		_cgi.erase(dead);

		if (_clients.count(clientFd))
		{
			_clients[clientFd].writeBuf =
				errorResponse(504, "Gateway Timeout", *_clients[clientFd].config);
			setPollEvents(clientFd, POLLOUT);
		}
	}
}

void	Server::checkClientTimeouts()
{
	time_t	now = time(NULL);

	std::map<int, Client>::iterator	it = _clients.begin();
	while (it != _clients.end())
	{
		int	fd = it->first;
		++it;
		if (now - _clients[fd].lastActivity >= CLIENT_TIMEOUT)
			closeClient(fd);
	}
}

void	Server::run()
{
	setupListeners();

	while (true)
	{
		if (poll(&_pfds[0], _pfds.size(), 1000) < 0)	// here's my array of sockets, here's how many, and 1000 means wait as long as it takes 
			throw std::runtime_error("poll() failed");

		checkCgiTimeouts();
		checkClientTimeouts();

		std::vector<struct pollfd>	ready = _pfds;
		for (size_t i = 0; i < ready.size(); ++i)	// iterate a copy so mid-loop adds and removes can't invalidate my iteration
		{
			if (ready[i].revents == 0)
				continue ;

			int	fd = ready[i].fd;
			if (_listeners.count(fd))	// revents is a bag of flags and we're checking whether one specific bit is set
				acceptClient(fd);
			else if (_cgi.count(fd))
				handleCgiRead(fd);
			else if (ready[i].revents & POLLIN)	// if it's readable:
				handleRead(fd);
			else if (ready[i].revents & POLLOUT)	// if it's writable:
				handleWrite(fd);
		}
	}
}

void	Server::acceptClient(int listenFd)
{
	int	clientFd = accept(listenFd, NULL, NULL);	// The two NULLs say "I don't care to record the caller's address"
	if (clientFd < 0)
		return ;

	fcntl(clientFd, F_SETFL, O_NONBLOCK);	// every socket we touch must be non-blocking

	Client	client;
	client.config = _listeners[listenFd];	// this client now knows which server block serves it
	client.lastActivity = time(NULL);
	_clients[clientFd] = client;

	addPollFd(clientFd, POLLIN);	// tell me when this caller speaks
}

void	Server::handleRead(int fd)
{
	char		buf[4096];	// read up to 4096 bytes into a stack buffer
	ssize_t	n = recv(fd, buf, sizeof(buf), 0);	// n is how many bytes actually came

	if (n <= 0)	// if true, just close the client
	{
		closeClient(fd);
		return ;
	}

	Client	&c = _clients[fd];
	c.readBuf.append(buf, n);	// slowly accumulates the request across however many recvs it takes
	c.lastActivity = time(NULL);

	// check the size before deciding the request is complete:
	if (bodyTooLarge(c.readBuf, c.config->maxBodySize))
	{
		c.writeBuf = errorResponse(413, "Payload Too Large", *c.config);
		setPollEvents(fd, POLLOUT);
		return ;
	}

	if (!isRequestComplete(c.readBuf)) // if we don't find it yet, the request is still arriving
		return ;

	Request	req = parseRequest(c.readBuf);

	std::string		fsPath;
	const Location	*loc = NULL;
	if (cgiTarget(req, *c.config, fsPath, &loc)) // CGI request → start it and return; poll takes over
	{
		startCgiFor(fd, req, loc, fsPath);
		return ;
	}

	c.writeBuf = buildResponse(req, *c.config);	// we park the finished bytes in c.writeBuf
	setPollEvents(fd, POLLOUT);	// the switch, *we are done reading*
}

void	Server::handleWrite(int fd)
{
	Client	&c = _clients[fd];
	ssize_t	n = send(fd, c.writeBuf.c_str(), c.writeBuf.size(), 0); // we send from writeBuf

	if (n <= 0)
	{
		closeClient(fd);
		return ;
	}

	c.writeBuf.erase(0, n);
	c.lastActivity = time(NULL);
}

void	Server::removePollFd(int fd)
{
	for (size_t i = 0; i < _pfds.size(); ++i)
	{
		if (_pfds[i].fd == fd)
		{
			_pfds.erase(_pfds.begin() + i);
			return ;
		}
	}
}

void	Server::setPollEvents(int fd, short ev)
{
	for (size_t i = 0; i < _pfds.size(); ++i)
	{
		if (_pfds[i].fd == fd)
		{
			_pfds[i].events = ev;
			return ;
		}
	}
}

void	Server::closeClient(int fd)
{
	for (std::map<int, CgiProcess>::iterator it = _cgi.begin(); it != _cgi.end(); ++it)
	{
		if (it->second.clientFd == fd)
		{
			kill(it->second.pid, SIGKILL);
			waitpid(it->second.pid, NULL, 0);
			close (it->first);
			removePollFd(it->first);
			_cgi.erase(it);
			break ;
		}
	}
	close(fd);
	_clients.erase(fd);
	removePollFd(fd);
}

void	Server::startCgiFor(int clientFd, const Request &req,
						const Location *loc, const std::string &fsPath)
{
	pid_t		pid;
	int		pipeFd;

	if (!startCgi(req, loc, fsPath, pid, pipeFd))
	{
		_clients[clientFd].writeBuf =
			errorResponse(500, "Internal Server Error", *_clients[clientFd].config);
		setPollEvents(clientFd, POLLOUT);
		return ;
	}

	CgiProcess	proc;
	proc.pid = pid;
	proc.clientFd = clientFd;
	proc.start = time(NULL);
	_cgi[pipeFd] = proc;

	addPollFd(pipeFd, POLLIN);
	setPollEvents(clientFd, 0);
}

void	Server::handleCgiRead(int pipeFd)
{
	CgiProcess	&proc = _cgi[pipeFd];
	char			buf[4096];
	ssize_t		n = read(pipeFd, buf, sizeof(buf));

	if (n > 0)
	{
		proc.output.append(buf, n);
		return ;
	}

	waitpid(proc.pid, NULL, 0);

	int	clientFd = proc.clientFd;
	if (_clients.count(clientFd))
	{
		_clients[clientFd].writeBuf = cgiResponse(proc.output, *_clients[clientFd].config);
		setPollEvents(clientFd, POLLOUT);
	}

	close(pipeFd);
	removePollFd(pipeFd);
	_cgi.erase(pipeFd);
}
