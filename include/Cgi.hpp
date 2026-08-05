#ifndef CGI_HPP
# define CGI_HPP

#include "Config.hpp"
#include "Http.hpp"
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <cstdlib>
#include <vector>
#include <sstream>
#include <fcntl.h>
#include <cstdio>

bool	startCgi(const Request &req, const std::string &cgiBin, const std::string &fsPath,
					pid_t &outPid, int &outFd);

#endif