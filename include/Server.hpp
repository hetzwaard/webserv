#ifndef SERVER_HPP
# define SERVER_HPP

#include "Config.hpp"
#include <vector>
#include <map>
#include <string>
#include <poll.h>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <sstream>

struct Client
{
	std::string				readBuf;
	std::string				writeBuf;

	const ServerConfig	*config;

	Client() : config(0) {}
};

class Server
{
private:
	std::vector<ServerConfig>				_servers;	// parsed config blocks (own them)
	std::vector<struct pollfd>				_pfds;		// the array poll() watches
	std::map<int, const ServerConfig*>	_listeners;	// listening fd -> its config
	std::map<int, Client>					_clients;	// connected fd  -> its state

	void setupListeners();									// socket/bind/listen for each server block
	void acceptClient(int listenFd);						// accept + register a new connection
	void handleRead(int fd);								// recv bytes; when request done, build response
	void handleWrite(int fd);								// send response bytes; close when drained
	void closeClient(int fd);								// close fd + erase from every container

	void addPollFd(int fd, short events);				// push a new fd into _pfds
	void setPollOut(int fd, bool on);					// flip a fd between watching POLLIN / POLLOUT

	Server(const Server &);									// not copyable — we hold raw fds
	Server &operator=(const Server &);

public:
	Server(const std::vector<ServerConfig> &servers);
	~Server();

	void	run();
};

#endif