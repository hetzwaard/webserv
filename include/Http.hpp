#ifndef HTTP_HPP
# define HTTP_HPP

#include "Config.hpp"
#include <string>
#include <sstream>
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <unistd.h>

struct Request
{
	std::string method;
	std::string path;
	std::string	query; // everything after '?' in the URL
	std::string	body;
	bool			valid;

	Request() : valid(false) {}
};

Request		parseRequest(const std::string &raw);
std::string	buildResponse(const Request &req, const ServerConfig &config);
bool			isRequestComplete(const std::string &raw);

#endif