#ifndef CGI_HPP
# define CGI_HPP

#include "Config.hpp"
#include "Http.hpp"
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>
#include <vector>
#include <sstream>

std::string	executeCgi(const Request &req, const Location *loc, const std::string &fsPath);

#endif