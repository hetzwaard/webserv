#ifndef SERVER_HPP
# define SERVER_HPP

#include "Config.hpp"
#include "Http.hpp"
#include "Cgi.hpp"
#include <vector>
#include <map>
#include <string>
#include <poll.h>
#include <iostream>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <csignal>
#include <ctime>

#define CGI_TIMEOUT 5
#define CLIENT_TIMEOUT 30

struct CgiProcess
{	
	pid_t				pid;			// the child running the script
	int				clientFd;	// which client is waiting on it
	std::string		output;		// bytes collected from the pipe so far
	time_t			start;		// when we forked (for the timeout, next layer)

	CgiProcess() : pid(-1), clientFd(-1), start(0) {}
};


struct Client
{
	std::string				readBuf;
	std::string				writeBuf;

	const ServerConfig	*config;

	time_t					lastActivity;

	Client() : config(0), lastActivity(0) {}
};

class Server
{
private:
	std::vector<ServerConfig>				_servers;	// parsed config blocks (own them)
	std::vector<struct pollfd>				_pfds;		// the array poll() watches
	std::map<int, const ServerConfig*>	_listeners;	// listening fd -> its config
	std::map<int, Client>					_clients;	// connected fd  -> its state
	std::map<int, CgiProcess>				_cgi;			// cgi pipe read fd -> its process

	void	setupListeners();									// socket/bind/listen for each server block
	void	acceptClient(int listenFd);						// accept + register a new connection
	void	handleRead(int fd);								// recv bytes; when request done, build response
	void	handleWrite(int fd);								// send response bytes; close when drained
	void	closeClient(int fd);								// close fd + erase from every container

	void	addPollFd(int fd, short events);				// push a new fd into _pfds

	void	startCgiFor(int clientFd, const Request &req,
							const std::string &fsPath, const std::string &cgiBin);
	void	handleCgiRead(int pipeFd);
	void	checkCgiTimeouts();
	void	checkClientTimeouts();
	void	removePollFd(int fd);							// erase an fd from _pfds
	void	setPollEvents(int fd, short ev);				// chage what we watch on an fd

	Server(const Server &);									// not copyable — we hold raw fds
	Server &operator=(const Server &);

public:
	Server(const std::vector<ServerConfig> &servers);
	~Server();

	void	run();
};

#endif