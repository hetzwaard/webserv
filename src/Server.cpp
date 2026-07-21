#include "../include/Server.hpp"
#include "../include/Http.hpp"

Server::Server(const std::vector<ServerConfig> &servers) : _servers(servers) {}

Server::~Server() {}

void	Server::addPollFd(int fd, short events)
{
	struct pollfd	pfd;

	pfd.fd = fd;				// which socket?
	pfd.events = events;		// what am I waiting for?
	pfd.revents = 0;			// the result (here what actually happened typo thing)
	_pfds.push_back(pfd);
}

void	Server::setupListeners()
{
	for (size_t i = 0; i < _servers.size(); ++i)
	{
		int	fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0)
			throw std::runtime_error(": socket() failed");

		int	opt = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));	// saving sanity level.. (lemme grap it again)

		struct sockaddr_in	addr;	// to know where to socket lives
		std::memset(&addr, 0, sizeof(addr));	// to zero first so no garbage bytes linger in the struct's padding
		addr.sin_family = AF_INET;	// IPv4 again
		addr.sin_addr.s_addr = inet_addr(_servers[i].host.c_str());	// converting 0.0.0.0 to kernel language
		addr.sin_port = htons(_servers[i].port);	// flips the bytes (safer in 16-bit numeric)
		
		if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)	// glues the socket to that address
			throw std::runtime_error(": bind() failed");

		if (listen(fd, SOMAXCONN) < 0)	// the socket now is waiting queue
			throw std::runtime_error(": listen() failed");

		fcntl(fd, F_SETFL, O_NONBLOCK);	// non-blocking socket method

		addPollFd(fd, POLLIN);	// tell me when someone knocks on
		_listeners[fd] = &_servers[i];	// this fd is one I accept on, and here's the config behind it

		std::cout << "Currently listening on " << _servers[i].host << ":" << _servers[i].port << std::endl;
	}
}

void	Server::run()
{
	setupListeners();

	while (true)
	{
		if (poll(&_pfds[0], _pfds.size(), -1) < 0)	// here's my array of sockets, here's how many, and -1 means wait as long as it takes 
			throw std::runtime_error(": poll() failed");

		std::vector<struct pollfd>	ready = _pfds;
		for (size_t i = 0; i < ready.size(); ++i)	// iterate a copy so mid-loop adds and removes can't invalidate my iteration
		{
			if (ready[i].revents == 0)
				continue ;

			int	fd = ready[i].fd;
			if (_listeners.count(fd))	// revents is a bag of flags and we're checking whether one specific bit is set
				acceptClient(fd);
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
	_clients[clientFd] = client;

	addPollFd(clientFd, POLLIN);	// tell me when this caller speaks

	std::cout << "Client connected (fd " << clientFd << ")" << std::endl;
}

void	Server::handleRead(int fd)
{
	char		buf[4096];	// read up to 4096 bytes into a stack buffer
	size_t	n = recv(fd, buf, sizeof(buf), 0);	// n is how many bytes actually came

	if (n <= 0)	// if true, just close the client
	{
		closeClient(fd);
		return ;
	}

	Client	&c = _clients[fd];
	c.readBuf.append(buf, n);	// slowly accumulates the request across however many recvs it takes

	if (!isRequestComplete(c.readBuf)) // if we don't find it yet, the request is still arriving
		return ;

	Request	req = parseRequest(c.readBuf);
	c.writeBuf = buildResponse(req, *c.config);	// we park the finished bytes in c.writeBuf
	setPollOut(fd, true);	// the switch, *we are done reading*
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
}

void	Server::closeClient(int fd)
{
	close(fd); // hands the OS phone line back.
	_clients.erase(fd); // drops the client's state (buffers, config pointer)

	for (size_t i = 0; i < _pfds.size(); ++i)
	{
		if (_pfds[i].fd == fd) // hunt thru _pfds for that fd-s card and erase it
		{
			_pfds.erase(_pfds.begin() + i);
			break ;
		}
	}
}

void	Server::setPollOut(int fd, bool on)
{
	for (size_t i = 0; i < _pfds.size(); ++i)
	{
		if (_pfds[i].fd == fd)
		{
			_pfds[i].events = on ? POLLOUT : POLLIN;
			return ;
		}
	}
}