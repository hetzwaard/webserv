#ifndef SERVER_HPP
# define SERVER_HPP

# include "server/Connection.hpp"

# include <poll.h>
# include <map>
# include <set>
# include <vector>
# include <cstddef>
# include <arpa/inet.h>
# include <netinet/in.h>
# include <sys/socket.h>
# include <fcntl.h>
# include <unistd.h>
# include <csignal>
# include <cstring>
# include <ctime>
# include <iostream>
# include <stdexcept>

class Server {
public:
	Server();
	~Server();

	void	addListener(int port);
	void	run();
	void	stop();

private:
	Server(const Server&);
	Server& operator=(const Server&);

	void	acceptNewClient(int listenFd);
	void	handleReadable(int fd);
	void	handleWritable(int fd);
	void	closeConnection(int fd);
	void	checkTimeouts();

	void	addFd(int fd, short events);
	void	removeFd(int fd);
	void	setPollEvents(int fd, short events);
	bool	isListener(int fd) const;

	std::vector<struct pollfd>	_fds;
	std::map<int, Connection>	_conns;
	std::set<int>				_listeners;

	static const int			POLL_TIMEOUT_MS = 1000;
	static const int			IDLE_TIMEOUT_SECONDS = 30;
	static const std::size_t	READ_CHUNK = 4096;
};

#endif
