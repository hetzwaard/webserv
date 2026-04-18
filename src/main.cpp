/* ************************************************************************** */
/*                                                                            */
/*                                                        ::::::::            */
/*   main.cpp                                           :+:    :+:            */
/*                                                     +:+                    */
/*   By: mahkilic <mahkilic@student.42.fr>            +#+                     */
/*                                                   +#+                      */
/*   Created: 2026/04/18 16:02:41 by mahkilic      #+#    #+#                 */
/*   Updated: 2026/04/18 16:02:41 by mahkilic      ########   odam.nl         */
/*                                                                            */
/* ************************************************************************** */

#include <iostream>
#include <cstring>	// for memset
#include <unistd.h>	// for close, read
#include <sys/socket.h>	// for socket, bind, listen, accept
#include <netinet/in.h>	// for sockaddr_in, htons, INADDR_ANY
#include <arpa/inet.h>	// for inet_ntoa

int main(void)
{
	// Step 1: create a socket
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) {
		std::cerr << "socket() failed" << std::endl;
		return (1);
	}

	// Step 2: describe what address/port to bind to
	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);  // "any local IP"
	addr.sin_port = htons(9090);			   // port 9090

	// Step 3: bind the socket to that address
	if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		std::cerr << "bind() failed" << std::endl;
		close(server_fd);
		return (1);
	}

	// Step 4: start listening for connections (backlog of 10 pending)
	if (listen(server_fd, 10) < 0) {
		std::cerr << "listen() failed" << std::endl;
		close(server_fd);
		return (1);
	}

	std::cout << "Listening on port 9090..." << std::endl;

	// Step 5: accept ONE connection (this blocks until a client connects)
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
	if (client_fd < 0) {
		std::cerr << "accept() failed" << std::endl;
		close(server_fd);
		return (1);
	}

	std::cout << "Client connected from " << inet_ntoa(client_addr.sin_addr) << std::endl;

	// Step 6: read whatever they send us
	char buffer[1024];
	std::memset(buffer, 0, sizeof(buffer));
	ssize_t bytes = read(client_fd, buffer, sizeof(buffer) - 1);
	if (bytes > 0) {
		std::cout << "Received " << bytes << " bytes:\n" << buffer << std::endl;
	}

	// Step 7: clean up
	close(client_fd);
	close(server_fd);
	return (0);
}
