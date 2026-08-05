#include "../include/Http.hpp"
#include "../include/Cgi.hpp"

Request	parseRequest(const std::string &raw)
{
	Request	req; // starts out valid = false

	std::string::size_type	lineEnd = raw.find("\r\n"); // locates the end of the first line
	if (lineEnd == std::string::npos) // special "not found" marker
		return (req);

	std::string line = raw.substr(0, lineEnd); // cuts out just the request line

	std::istringstream	iss(line);
	std::string				version;
	if (!(iss >> req.method >> req.path >> version))
		return (req);

	std::string::size_type	headerEnd = raw.find("\r\n\r\n");
	if (headerEnd != std::string::npos)
		req.body = raw.substr(headerEnd + 4);

	std::string::size_type	q = req.path.find('?');
	if (q != std::string::npos)
	{
		req.query = req.path.substr(q + 1);
		req.path = req.path.substr(0, q);
	}

	std::string::size_type	ck = raw.find("Cookie:");
	if (ck != std::string::npos && ck < headerEnd)
	{
		std::string::size_type	lineEnd = raw.find("\r\n", ck);
		std::string::size_type	valStart = ck + 7;	// past "Cookie:"

		while (valStart < lineEnd && raw[valStart] == ' ')
			++valStart;

		req.cookie = raw.substr(valStart, lineEnd - valStart);
	}

	req.valid = true;
	return (req);
}

bool	bodyTooLarge(const std::string &raw, size_t maxBody)
{
	std::string::size_type	headerEnd = raw.find("\r\n\r\n");	// no blank line yet, keep on going
	if (headerEnd == std::string::npos)
		return (false);

	// Before the header end, if it's there and the declared size exceeds the limit, reject immdtly.
	std::string::size_type	clPos = raw.find("Content-Length:");
	if (clPos != std::string::npos && clPos < headerEnd)
	{
		size_t	declared = (size_t)std::atol(raw.c_str() + clPos + 15);
		if (declared > maxBody)
			return (true);
	}
	return (raw.size() - (headerEnd + 4) > maxBody);
}

bool	isRequestComplete(const std::string &raw)
{
	std::string::size_type	headerEnd = raw.find("\r\n\r\n"); // no blank line yet means the headers haven't even finished
	if (headerEnd == std::string::npos)
		return (false);

	std::string::size_type	clPos = raw.find("Content-Length:"); // if it's not there, would mean it's in the body
	if (clPos == std::string::npos || clPos > headerEnd)
		return (true);

	size_t	contentLength = std::atoi(raw.c_str() + clPos + 15); // + 15 skips past "Content-Length:"
	std::string::size_type	bodyStart = headerEnd + 4; // the body begins 4 bytes after where \r\n\r\n

	return (raw.size() - bodyStart >= contentLength);
}

static const Location	*matchLocation(const Request &req, const ServerConfig &config)
{
	const Location	*best = 0;

	for (size_t i = 0; i < config.locations.size(); ++i)
	{
		const std::string	&lp = config.locations[i].path;
		if (req.path.compare(0, lp.size(), lp) == 0) // does the request path start with this location's path?
		{
			if (!best || lp.size() > best->path.size())
				best = &config.locations[i];
		}
	}
	return (best);
}

static std::string	contentType(const std::string &path)
{
	std::string::size_type	dot = path.rfind('.'); // finds the last dot
	if (dot == std::string::npos)
		return "application/octet-stream";
	std::string	ext = path.substr(dot);

	if (ext == ".html" || ext == ".htm")	return ("text/html");
	if (ext == ".css")							return ("text/css");
	if (ext == ".js")								return ("application/javascript");
	if (ext == ".png")							return ("image/png");
	if (ext == ".jpg" || ext == ".jpeg")	return ("image/jpeg");
	if (ext == ".gif")							return ("image/gif");
	if (ext == ".txt")							return ("text/plain");

	return ("application/octet-stream"); // anything unknown falls back to application/octet-stream
}

static bool	readFile(const std::string &path, std::string &out)
{
	struct stat	st;

	// ifstream opens a directory successfully on libstdc++ and only throws when
	// read, so is_open() is not enough — reject anything that isn't a regular file
	if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
		return (false);

	std::ifstream	file(path.c_str(), std::ios::binary);

	if (!file.is_open())
		return (false);

	out.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	return (true);
}

static std::string	makeResponse(int code, const std::string &status,
	const std::string &type, const std::string &body)
{
	std::ostringstream	resp;

	resp	<< "HTTP/1.1 " << code << " " << status << "\r\n"
			<< "Content-Type: " << type << "\r\n"
			<< "Content-Length: " << body.size() << "\r\n"
			<< "Connection: close\r\n"
			<< "\r\n"
			<< body;

	return (resp.str());
}

static std::string	toStr(int n)
{
	std::ostringstream	oss;
	oss << n;
	return oss.str();
}

static std::string	stripToRoot(const std::string &reqPath, const Location *loc)
{
	std::string	rest = reqPath.substr(loc->path.size());
	if (rest.empty() || rest[0] != '/')
		rest = "/" + rest;
	return (loc->root + rest);
}

static std::string	resolvePath(const std::string &reqPath, const Location *loc)
{
	std::string	full = stripToRoot(reqPath, loc);
	if (full[full.size() - 1] == '/')
		full += loc->index;
	return (full);
}

static bool	listDirectory(const std::string &dirPath, const std::string &urlPath,
					std::string &out)
{
	DIR	*dir = opendir(dirPath.c_str());
	if (!dir)
		return (false);

	std::string		body = "<h1>Index of " + urlPath + "</h1>\n<ul>\n";
	struct dirent	*entry;

	while ((entry = readdir(dir)) != NULL)
	{
		std::string	name = entry->d_name;
		if (name == ".")
			continue ;
		body += "<li><a href=\"" + urlPath + name + "\">" + name + "</a></li>\n";
	}
	closedir(dir);

	body += "</ul>\n";
	out = body;
	return (true);
}

std::string	errorResponse(int code, const std::string &status, const ServerConfig &config)
{
	std::map<int, std::string>::const_iterator	it = config.errorPages.find(code);

	if (it != config.errorPages.end())
	{
		std::string	body;
		if (readFile(it->second, body))
			return (makeResponse(code, status, "text/html", body));
	}
	std::string	fallback = "<h1>" + toStr(code) + " " + status + "</h1>\n";
	return (makeResponse(code, status, "text/html", fallback));
}

static std::string	handleDelete(const std::string &fsPath, const ServerConfig &config)
{
	std::ifstream	test(fsPath.c_str());

	if (!test.is_open()) // first we check the file exists
		return (errorResponse(404, "Not Found", config));
	test.close(); // we close it immediately

	if (std::remove(fsPath.c_str()) != 0) // it returns 0 on success, non-zero on failure
		return (errorResponse(403, "Forbidden", config));

	return (makeResponse(200, "OK", "text/html", "<h1>File deleted</h1>\n")); // Success → 200
}

static std::string	handleUpload(const Request &req, const Location *loc, const ServerConfig &config)
{
	if (loc->uploadDir.empty())
		return (errorResponse(403, "Forbidden", config)); // if this location has no uploadDir configured, uploads aren't allowed here

	std::string::size_type	slash = req.path.rfind('/'); // finds the last slash in the path
	std::string					filename = req.path.substr(slash + 1); // and substr(slash + 1) takes everything after it
	if (filename.empty())
		return (errorResponse(400, "Bad Request", config));

	std::string	dest = loc->uploadDir + "/" + filename; // builds the destination path

	std::ofstream	out(dest.c_str(), std::ios::binary);
	if (!out.is_open())
		return (errorResponse(500, "Internal Server Error", config));
	out.write(req.body.c_str(), req.body.size()); // dumps the entire body to disk
	out.close();

	return (makeResponse(201, "Created", "text/html", "<h1>File uploaded</h1>\n")); // "your POST made a new resource," more precise than a plain 200
}

static bool	isCgi(const std::string &fsPath, const Location *loc, std::string &binOut)
{
	std::map<std::string, std::string>::const_iterator	it;

	for (it = loc->cgi.begin(); it != loc->cgi.end(); ++it)
	{
		const std::string	&ext = it->first;
		if (fsPath.size() >= ext.size() && fsPath.compare(fsPath.size() - ext.size(), ext.size(), ext) == 0)
		{
			binOut = it->second;
			return (true);
		}
	}
	return (false);
}

bool	cgiTarget(const Request &req, const ServerConfig &config,
				std::string &fsPath, const Location **outLoc, std::string &cgiBin)
{
	if (!req.valid || req.path.find("..") != std::string::npos)
		return (false);

	const Location	*loc = matchLocation(req, config);
	if (!loc)
		return (false);

	bool	allowed = false;
	for (size_t i = 0; i < loc->methods.size(); ++i)
		if (loc->methods[i] == req.method)
			allowed = true;
	if (!allowed)
		return (false);

	std::string	path = resolvePath(req.path, loc);

	if (!isCgi(path, loc, cgiBin))
		return (false);

	fsPath = path;
	*outLoc = loc;

	return (true);
}

std::string	cgiResponse(const std::string &cgiOut, const ServerConfig &config)
{
	if (cgiOut.empty())
		return (errorResponse(500, "Internal Server Error", config));

	std::string::size_type	sep = cgiOut.find("\r\n\r\n");
	std::string::size_type	seplen = 4;
	if (sep == std::string::npos)
	{
		sep = cgiOut.find("\n\n");
		seplen = 2;
	}

	std::string	cgiHeaders;
	std::string	cgiBody;

	if (sep != std::string::npos)
	{
		cgiHeaders = cgiOut.substr(0, sep);
		cgiBody = cgiOut.substr(sep + seplen);
	}
	else
		cgiBody = cgiOut;

	std::ostringstream	resp;
	resp	<< "HTTP/1.1 200 OK\r\n"
			<< cgiHeaders << "\r\n"
			<< "Content-Length: " << cgiBody.size() << "\r\n"
			<< "Connection: close\r\n"
			<< "\r\n"
			<< cgiBody;

	return (resp.str());
}

std::string	buildResponse(const Request &req, const ServerConfig &config)
{
	// if the request didn't even parse, it's garbage, 400.
	if (!req.valid)
		return (errorResponse(400, "Bad Request", config));

	// if the path contains "..", refuse with 403.
	if (req.path.find("..") != std::string::npos)
		return (errorResponse(403, "Forbidden", config));

	// No match -> 404
	const Location	*loc = matchLocation(req, config);
	if (!loc)
		return (errorResponse(404, "Not Found", config));

	if (!loc->redirect.empty())
	{
		std::ostringstream	resp;
		std::string			body = "<h1>301 Moved Permanently</h1>\n";
		resp	<< "HTTP/1.1 301 Moved Permanently\r\n"
				<< "Location: " << loc->redirect << "\r\n"
				<< "Content-Type: text/html\r\n"
				<< "Content-Length: " << body.size() << "\r\n"
				<< "Connection: close\r\n"
				<< "\r\n"
				<< body;
		return (resp.str());
	}

	bool	allowed = false; // allowed method check
	for (size_t i = 0; i < loc->methods.size(); ++i)
		if (loc->methods[i] == req.method)
			allowed = true;
	if (!allowed)
		return (errorResponse(405, "Method Not Allowed", config));

	std::string	fsPath = resolvePath(req.path, loc);

	if (req.method == "DELETE")
		return (handleDelete(fsPath, config));

	if (req.method == "GET")
	{
		std::string	body;
		if (readFile(fsPath, body))
			return (makeResponse(200, "OK", contentType(fsPath), body));

		if (loc->autoindex && !req.path.empty()
			&& req.path[req.path.size() - 1] == '/')
		{
			std::string	listing;
			if (listDirectory(stripToRoot(req.path, loc), req.path, listing))
				return (makeResponse(200, "OK", "text/html", listing));
		}

		return (errorResponse(404, "Not Found", config));
	}
	
	if (req.method == "POST")
		return (handleUpload(req, loc, config));

	return (errorResponse(501, "Not Implemented", config));
}
