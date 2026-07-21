#ifndef CONFIG_HPP
# define CONFIG_HPP

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <stdexcept>
#include <cstdlib>

struct Location
{
	std::string						path;			// "/"
	std::string						root;			// "./www"
	std::string						index;		// "index.html"
	std::string						redirect;	// non-empty -> 301
	std::string						uploadDir;	// where POST uploads land
	std::string						cgiExt;		// ".py"
	std::string						cgiBin;		// "/usr/bin/python3"

	std::vector<std::string>	methods;		// GET POST DELETE

	bool								autoindex;	// directory listing

	Location() : autoindex(false) {}
};

struct ServerConfig
{
	std::string						host;
	std::string						serverName;

	size_t							port;
	size_t							maxBodySize;

	std::map<int, std::string>	errorPages;	// code -> file path
	std::vector<Location>		locations;

	ServerConfig() : port(80), maxBodySize(1048576) {}
};

std::vector<ServerConfig> parseConfig(const std::string &path);

#endif