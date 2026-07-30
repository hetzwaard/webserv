#ifndef HTTP_HPP
# define HTTP_HPP

#include "Config.hpp"
#include <string>
#include <sstream>
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <unistd.h>
#include <dirent.h>

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
bool			bodyTooLarge(const std::string &raw, size_t maxBody);
std::string	errorResponse(int code, const std::string &status, const ServerConfig &config);
std::string	cgiResponse(const std::string &cgiOut, const ServerConfig &config);
bool			cgiTarget(const Request &req, const ServerConfig &config,
				std::string &fsPath, const Location **outLoc, std::string &cgiBin);

#endif