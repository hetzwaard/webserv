#include "server/Server.hpp"
#include "http/HttpResponse.hpp"

#include <sstream>

namespace
{
	volatile sig_atomic_t g_stop = 0;
	void on_signal(int)
	{
		g_stop = 1;
	}
}

Server::Server()
{
	std::signal(SIGINT, on_signal);
	std::signal(SIGTERM, on_signal);
	std::signal(SIGPIPE, SIG_IGN);
}

Server::~Server()
{
	for (std::size_t i = 0; i < _fds.size(); ++i)::close(_fds[i].fd);
}

void	Server::addListener(int port)
{
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		throw std::runtime_error("socket() failed");

	int yes = 1;
	if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
	{
		::close(fd);
		throw std::runtime_error("setsockopt() failed");
	}
	if (::fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
	{
		::close(fd);
		throw std::runtime_error("fcntl(O_NONBLOCK) failed");
	}

	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(static_cast<unsigned short>(port));

	if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
	{
		::close(fd);
		throw std::runtime_error("bind() failed");
	}
	if (::listen(fd, 128) < 0)
	{
		::close(fd);
		throw std::runtime_error("listen() failed");
	}

	addFd(fd, POLLIN);
	_listeners.insert(fd);
	std::cout << "Listening on port " << port << " (fd " << fd << ")" << std::endl;
}

void	Server::run()
{
	while (!g_stop)
	{
		int n = ::poll(_fds.empty() ? NULL : &_fds[0], _fds.size(), POLL_TIMEOUT_MS);
		if (n < 0)
		{
			if (g_stop) break;
			std::cerr << "poll() returned -1; shutting down" << std::endl;
			break;
		}

		std::vector<struct pollfd> snapshot = _fds;
		for (std::size_t i = 0; i < snapshot.size(); ++i)
		{
			int	  fd = snapshot[i].fd;
			short re = snapshot[i].revents;
			if (re == 0)
				continue;
			if (!isListener(fd) && _conns.find(fd) == _conns.end())
				continue;

			if (re & (POLLHUP | POLLERR | POLLNVAL))
			{
				if (!isListener(fd))
					closeConnection(fd);
				continue;
			}
			if (re & POLLIN)
			{
				if (isListener(fd))	acceptNewClient(fd);
				else				handleReadable(fd);
			}
			if ((re & POLLOUT) && _conns.find(fd) != _conns.end())
				handleWritable(fd);
		}

		checkTimeouts();
	}
	std::cout << "\nShutting down." << std::endl;
}

void	Server::stop()
{
	g_stop = 1;
}

void	Server::acceptNewClient(int listenFd)
{
	struct sockaddr_in	caddr;
	socklen_t			clen = sizeof(caddr);
	int cfd = ::accept(listenFd, reinterpret_cast<struct sockaddr*>(&caddr), &clen);
	if (cfd < 0)
		return;
	if (::fcntl(cfd, F_SETFL, O_NONBLOCK) < 0)
	{
		::close(cfd);
		return;
	}
	_conns[cfd] = Connection(cfd);
	addFd(cfd, POLLIN);
	std::cout << "Accepted client fd " << cfd << " from " << inet_ntoa(caddr.sin_addr) << std::endl;
}

void	Server::handleReadable(int fd)
{
	std::map<int, Connection>::iterator it = _conns.find(fd);
	if (it == _conns.end())
		return;
	Connection& c = it->second;

	char	buf[READ_CHUNK];
	ssize_t	n = ::recv(fd, buf, sizeof(buf), 0);
	if (n <= 0)
	{
		closeConnection(fd);
		return;
	}
	c.touch();
	c.request().feed(buf, static_cast<std::size_t>(n));

	if (c.request().isError())
	{
		buildErrorResponse(c, c.request().errorCode());
		c.setState(Connection::WRITING_RESPONSE);
		setPollEvents(fd, POLLOUT);
		return;
	}
	if (c.request().isComplete())
	{
		std::cout << "Request: " << c.request().method()
				<< " " << c.request().uri()
				<< " " << c.request().version()
				<< " (body " << c.request().body().size() << "B)" << std::endl;
		buildOkResponse(c);
		c.setState(Connection::WRITING_RESPONSE);
		setPollEvents(fd, POLLOUT);
	}
}

void	Server::handleWritable(int fd)
{
	std::map<int, Connection>::iterator it = _conns.find(fd);
	if (it == _conns.end())
		return;
	Connection& c = it->second;

	const std::string&	buf = c.writeBuf();
	std::size_t			offset = c.bytesSent();
	if (offset >= buf.size())
	{
		closeConnection(fd);
		return;
	}

	ssize_t n = ::send(fd, buf.data() + offset, buf.size() - offset, 0);
	if (n <= 0)
	{
		closeConnection(fd);
		return;
	}
	c.touch();
	c.addBytesSent(static_cast<std::size_t>(n));

	if (c.writeComplete())
		closeConnection(fd);
}

void	Server::buildOkResponse(Connection& c)
{
	const HttpRequest& req = c.request();
	std::ostringstream body;
	body << "<!DOCTYPE html><html><head><title>webserv</title></head>"
		<< "<body style=\"font-family:sans-serif;\">"
		<< "<h1>webserv is alive.</h1>"
		<< "<p>Method: <b>" << req.method() << "</b></p>"
		<< "<p>URI: <b>" << req.uri() << "</b></p>"
		<< "<p>Version: <b>" << req.version() << "</b></p>"
		<< "<p>Body size: " << req.body().size() << " bytes</p>"
		<< "</body></html>";

	HttpResponse resp(200);
	resp.setHeader("Content-Type", "text/html; charset=utf-8");
	resp.setBody(body.str());

	c.writeBuf() = resp.serialize();
	c.resetBytesSent();
}

void	Server::buildErrorResponse(Connection& c, int code)
{
	HttpResponse resp(code);
	resp.setHeader("Content-Type", "text/html; charset=utf-8");
	resp.setBody(HttpResponse::defaultErrorBody(code));

	c.writeBuf() = resp.serialize();
	c.resetBytesSent();
}

void	Server::closeConnection(int fd)
{
	::close(fd);
	_conns.erase(fd);
	removeFd(fd);
	std::cout << "Closed fd " << fd << std::endl;
}

void	Server::checkTimeouts()
{
	std::time_t		now = std::time(NULL);
	std::vector<int>	expired;
	for (std::map<int, Connection>::iterator it = _conns.begin(); it != _conns.end(); ++it)
	{
		if (now - it->second.lastActivity() > IDLE_TIMEOUT_SECONDS)
			expired.push_back(it->first);
	}
	for (std::size_t i = 0; i < expired.size(); ++i)
	{
		std::cout << "Idle timeout on fd " << expired[i] << std::endl;
		closeConnection(expired[i]);
	}
}

void	Server::addFd(int fd, short events)
{
	struct pollfd pfd;
	pfd.fd = fd;
	pfd.events = events;
	pfd.revents = 0;
	_fds.push_back(pfd);
}

void	Server::removeFd(int fd)
{
	for (std::size_t i = 0; i < _fds.size(); ++i)
	{
		if (_fds[i].fd == fd)
		{
			_fds.erase(_fds.begin() + i);
			return;
		}
	}
}

void	Server::setPollEvents(int fd, short events)
{
	for (std::size_t i = 0; i < _fds.size(); ++i)
	{
		if (_fds[i].fd == fd)
		{
			_fds[i].events = events;
			return;
		}
	}
}

bool	Server::isListener(int fd) const
{
	return _listeners.find(fd) != _listeners.end();
}
