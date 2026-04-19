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

Server::Server() : _docRoot("./www"), _uploadDir("./www/uploads")
{
	std::signal(SIGINT, on_signal);
	std::signal(SIGTERM, on_signal);
	std::signal(SIGPIPE, SIG_IGN);
	// Ensure upload directory exists
	::mkdir(_uploadDir.c_str(), 0755);
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
		handleRequest(c);
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
	{
		if (c.shouldKeepAlive())
		{
			c.resetForNextRequest();
			setPollEvents(fd, POLLIN);
		}
		else
			closeConnection(fd);
	}
}

/* ─── Request dispatch ─── */

void	Server::handleRequest(Connection& c)
{
	const std::string& method = c.request().method();
	std::string uri = c.request().uri();
	std::string::size_type qpos = uri.find('?');
	std::string pathPart = (qpos != std::string::npos) ? uri.substr(0, qpos) : uri;

	// ── Redirects (hardcoded for demo — will come from config) ──
	if (pathPart == "/old-page")
		return buildRedirect(c, 301, "/");
	if (pathPart == "/redirect-me")
		return buildRedirect(c, 302, "/");

	// ── Upload paths: POST and DELETE only ──
	bool isUploadPath = (pathPart.find("/uploads/") == 0 && pathPart.size() > 9);

	if (method == "GET")
	{
		std::string path = resolvePath(uri);
		if (path.empty())
			return buildErrorResponse(c, 400);
		if (!isPathSafe(_docRoot, path))
			return buildErrorResponse(c, 403);

		struct stat st;
		if (::stat(path.c_str(), &st) < 0)
			return buildErrorResponse(c, 404);

		if (S_ISDIR(st.st_mode))
		{
			if (pathPart[pathPart.size() - 1] != '/')
				return buildRedirect(c, 301, pathPart + "/");
			std::string indexPath = path + "/index.html";
			if (::stat(indexPath.c_str(), &st) == 0 && S_ISREG(st.st_mode))
				return serveFile(c, indexPath);
			return serveDirectory(c, path, pathPart);
		}
		return serveFile(c, path);
	}
	else if (method == "POST")
	{
		if (!isUploadPath)
			return buildErrorResponse(c, 405);
		return handleUpload(c);
	}
	else if (method == "DELETE")
	{
		if (!isUploadPath)
			return buildErrorResponse(c, 405);
		return handleDeleteMethod(c);
	}
	else
		return buildErrorResponse(c, 501);
}

/* ─── Serve a single file ─── */

void	Server::serveFile(Connection& c, const std::string& filePath)
{
	int fd = ::open(filePath.c_str(), O_RDONLY);
	if (fd < 0)
		return buildErrorResponse(c, 403);

	// Read file contents (regular disk files are exempt from poll)
	std::string body;
	char buf[READ_CHUNK];
	ssize_t n;
	while ((n = ::read(fd, buf, sizeof(buf))) > 0)
		body.append(buf, static_cast<std::size_t>(n));
	::close(fd);

	HttpResponse resp(200);
	resp.setHeader("Content-Type", mimeType(filePath));
	resp.setHeader("Connection", c.shouldKeepAlive() ? "keep-alive" : "close");
	resp.setBody(body);

	c.writeBuf() = resp.serialize();
	c.resetBytesSent();
}

/* ─── Directory listing (autoindex) ─── */

void	Server::serveDirectory(Connection& c, const std::string& dirPath,
								const std::string& uri)
{
	DIR* dir = ::opendir(dirPath.c_str());
	if (!dir)
		return buildErrorResponse(c, 403);

	std::ostringstream html;
	html << "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
		 << "<title>Index of " << uri << "</title>"
		 << "<style>"
		 << "body{font-family:monospace;background:#0a0a1a;color:#e8e8f0;padding:2rem;}"
		 << "h1{color:#7c5cfc;}a{color:#00d4aa;text-decoration:none;}"
		 << "a:hover{text-decoration:underline;}"
		 << "table{border-collapse:collapse;width:100%;max-width:800px;}"
		 << "td,th{padding:0.4rem 1rem;text-align:left;border-bottom:1px solid #222;}"
		 << "</style></head><body>"
		 << "<h1>Index of " << uri << "</h1><table>"
		 << "<tr><th>Name</th><th>Size</th></tr>";

	if (uri != "/")
		html << "<tr><td><a href=\"../\">..</a></td><td>-</td></tr>";

	struct dirent* entry;
	while ((entry = ::readdir(dir)) != NULL)
	{
		std::string name = entry->d_name;
		if (name == "." || name == "..")
			continue;

		std::string fullPath = dirPath + "/" + name;
		struct stat st;
		std::string suffix;
		std::string sizeStr = "-";

		if (::stat(fullPath.c_str(), &st) == 0)
		{
			if (S_ISDIR(st.st_mode))
				suffix = "/";
			else
			{
				std::ostringstream ss;
				if (st.st_size < 1024)
					ss << st.st_size << " B";
				else if (st.st_size < 1024 * 1024)
					ss << (st.st_size / 1024) << " KB";
				else
					ss << (st.st_size / (1024 * 1024)) << " MB";
				sizeStr = ss.str();
			}
		}
		html << "<tr><td><a href=\"" << name << suffix << "\">"
			 << name << suffix << "</a></td><td>" << sizeStr << "</td></tr>";
	}
	::closedir(dir);

	html << "</table><hr><p style=\"color:#666;\">webserv</p></body></html>";

	HttpResponse resp(200);
	resp.setHeader("Content-Type", "text/html; charset=utf-8");
	resp.setHeader("Connection", c.shouldKeepAlive() ? "keep-alive" : "close");
	resp.setBody(html.str());

	c.writeBuf() = resp.serialize();
	c.resetBytesSent();
}

/* ─── POST file upload ─── */

void	Server::handleUpload(Connection& c)
{
	std::string uri = c.request().uri();
	std::string::size_type qpos = uri.find('?');
	if (qpos != std::string::npos)
		uri = uri.substr(0, qpos);

	// Only allow uploads under /uploads/
	if (uri.find("/uploads/") != 0 || uri.size() <= 9)
		return buildErrorResponse(c, 403);

	std::string filename = percentDecode(uri.substr(9));
	if (filename.empty() || filename.find("..") != std::string::npos
		|| filename.find('/') != std::string::npos)
		return buildErrorResponse(c, 400);

	std::string fullPath = _uploadDir + "/" + filename;

	int fd = ::open(fullPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return buildErrorResponse(c, 500);

	const std::string& body = c.request().body();
	const char* data = body.data();
	std::size_t remaining = body.size();
	while (remaining > 0)
	{
		ssize_t n = ::write(fd, data, remaining);
		if (n < 0)
		{
			::close(fd);
			return buildErrorResponse(c, 500);
		}
		data += n;
		remaining -= static_cast<std::size_t>(n);
	}
	::close(fd);

	std::ostringstream respBody;
	respBody << "{\"status\":\"uploaded\",\"file\":\"" << filename
			 << "\",\"size\":" << body.size() << "}";

	HttpResponse resp(201);
	resp.setHeader("Content-Type", "application/json");
	resp.setHeader("Connection", c.shouldKeepAlive() ? "keep-alive" : "close");
	resp.setBody(respBody.str());

	c.writeBuf() = resp.serialize();
	c.resetBytesSent();
}

/* ─── DELETE ─── */

void	Server::handleDeleteMethod(Connection& c)
{
	std::string uri = c.request().uri();
	std::string::size_type qpos = uri.find('?');
	if (qpos != std::string::npos)
		uri = uri.substr(0, qpos);

	// Only allow deletes under /uploads/
	if (uri.find("/uploads/") != 0 || uri.size() <= 9)
		return buildErrorResponse(c, 403);

	std::string filename = percentDecode(uri.substr(9));
	if (filename.empty() || filename.find("..") != std::string::npos
		|| filename.find('/') != std::string::npos)
		return buildErrorResponse(c, 400);

	std::string fullPath = _uploadDir + "/" + filename;

	struct stat st;
	if (::stat(fullPath.c_str(), &st) < 0)
		return buildErrorResponse(c, 404);
	if (!S_ISREG(st.st_mode))
		return buildErrorResponse(c, 403);

	if (std::remove(fullPath.c_str()) != 0)
		return buildErrorResponse(c, 500);

	HttpResponse resp(200);
	resp.setHeader("Content-Type", "application/json");
	resp.setHeader("Connection", c.shouldKeepAlive() ? "keep-alive" : "close");
	resp.setBody("{\"status\":\"deleted\",\"file\":\"" + filename + "\"}");

	c.writeBuf() = resp.serialize();
	c.resetBytesSent();
}

/* ─── Redirect response ─── */

void	Server::buildRedirect(Connection& c, int code, const std::string& location)
{
	HttpResponse resp(code);
	resp.setHeader("Location", location);
	resp.setHeader("Connection", c.shouldKeepAlive() ? "keep-alive" : "close");
	resp.setBody("");

	c.writeBuf() = resp.serialize();
	c.resetBytesSent();
}

/* ─── Error response ─── */

void	Server::buildErrorResponse(Connection& c, int code)
{
	HttpResponse resp(code);
	resp.setHeader("Content-Type", "text/html; charset=utf-8");
	resp.setHeader("Connection", "close");
	resp.setBody(HttpResponse::defaultErrorBody(code));

	c.writeBuf() = resp.serialize();
	c.resetBytesSent();
}

/* ─── URI → filesystem path ─── */

std::string	Server::resolvePath(const std::string& uri) const
{
	// Split off query string
	std::string path = uri;
	std::string::size_type qpos = path.find('?');
	if (qpos != std::string::npos)
		path = path.substr(0, qpos);

	path = percentDecode(path);

	// Normalize: collapse // and resolve . and ..
	std::vector<std::string> parts;
	std::string segment;
	for (std::size_t i = 0; i < path.size(); ++i)
	{
		if (path[i] == '/')
		{
			if (segment == "..")
			{
				if (!parts.empty())
					parts.pop_back();
			}
			else if (!segment.empty() && segment != ".")
				parts.push_back(segment);
			segment.clear();
		}
		else
			segment += path[i];
	}
	// Handle last segment
	if (segment == "..")
	{
		if (!parts.empty())
			parts.pop_back();
	}
	else if (!segment.empty() && segment != ".")
		parts.push_back(segment);

	std::string resolved = _docRoot;
	for (std::size_t i = 0; i < parts.size(); ++i)
		resolved += "/" + parts[i];

	return resolved;
}

std::string	Server::percentDecode(const std::string& s)
{
	std::string result;
	for (std::size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '%' && i + 2 < s.size())
		{
			int hi = 0, lo = 0;
			char c1 = s[i + 1], c2 = s[i + 2];
			if (c1 >= '0' && c1 <= '9')		hi = c1 - '0';
			else if (c1 >= 'a' && c1 <= 'f')	hi = c1 - 'a' + 10;
			else if (c1 >= 'A' && c1 <= 'F')	hi = c1 - 'A' + 10;
			else { result += s[i]; continue; }
			if (c2 >= '0' && c2 <= '9')		lo = c2 - '0';
			else if (c2 >= 'a' && c2 <= 'f')	lo = c2 - 'a' + 10;
			else if (c2 >= 'A' && c2 <= 'F')	lo = c2 - 'A' + 10;
			else { result += s[i]; continue; }
			result += static_cast<char>(hi * 16 + lo);
			i += 2;
		}
		else
			result += s[i];
	}
	return result;
}

bool	Server::isPathSafe(const std::string& root, const std::string& path)
{
	// Ensure resolved path starts with the document root
	if (path.size() < root.size())
		return false;
	return path.compare(0, root.size(), root) == 0;
}

std::string	Server::mimeType(const std::string& path)
{
	std::string::size_type dot = path.rfind('.');
	if (dot == std::string::npos)
		return "application/octet-stream";

	std::string ext = path.substr(dot);
	if (ext == ".html" || ext == ".htm")	return "text/html; charset=utf-8";
	if (ext == ".css")						return "text/css";
	if (ext == ".js")						return "application/javascript";
	if (ext == ".json")						return "application/json";
	if (ext == ".png")						return "image/png";
	if (ext == ".jpg" || ext == ".jpeg")	return "image/jpeg";
	if (ext == ".gif")						return "image/gif";
	if (ext == ".svg")						return "image/svg+xml";
	if (ext == ".ico")						return "image/x-icon";
	if (ext == ".txt")						return "text/plain";
	if (ext == ".pdf")						return "application/pdf";
	if (ext == ".xml")						return "application/xml";
	if (ext == ".webp")						return "image/webp";
	if (ext == ".woff")						return "font/woff";
	if (ext == ".woff2")					return "font/woff2";
	return "application/octet-stream";
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
