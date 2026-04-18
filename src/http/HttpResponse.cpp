#include "http/HttpResponse.hpp"

#include <sstream>

HttpResponse::HttpResponse() : _status(200) {}

HttpResponse::HttpResponse(int status) : _status(status) {}

void	HttpResponse::setStatus(int code)									{ _status = code; }
void	HttpResponse::setHeader(const std::string& n, const std::string& v)	{ _headers[n] = v; }
void	HttpResponse::setBody(const std::string& b)							{ _body = b; }

int					HttpResponse::status() const	{ return _status; }
const std::string&	HttpResponse::body() const		{ return _body; }

std::string	HttpResponse::serialize() const
{
	std::ostringstream oss;
	oss << "HTTP/1.1 " << _status << " " << reasonPhrase(_status) << "\r\n";

	HeaderMap h = _headers;
	if (h.find("Content-Length") == h.end())
	{
		std::ostringstream cl;
		cl << _body.size();
		h["Content-Length"] = cl.str();
	}
	if (h.find("Connection") == h.end())
		h["Connection"] = "close";
	if (h.find("Server") == h.end())
		h["Server"] = "webserv/0.1";

	for (HeaderMap::const_iterator it = h.begin(); it != h.end(); ++it)
		oss << it->first << ": " << it->second << "\r\n";
	oss << "\r\n";
	oss << _body;
	return oss.str();
}

std::string	HttpResponse::reasonPhrase(int code)
{
	switch (code)
	{
		case 200: return "OK";
		case 201: return "Created";
		case 204: return "No Content";
		case 301: return "Moved Permanently";
		case 302: return "Found";
		case 303: return "See Other";
		case 304: return "Not Modified";
		case 307: return "Temporary Redirect";
		case 308: return "Permanent Redirect";
		case 400: return "Bad Request";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 408: return "Request Timeout";
		case 411: return "Length Required";
		case 413: return "Payload Too Large";
		case 414: return "URI Too Long";
		case 415: return "Unsupported Media Type";
		case 431: return "Request Header Fields Too Large";
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 502: return "Bad Gateway";
		case 503: return "Service Unavailable";
		case 504: return "Gateway Timeout";
		case 505: return "HTTP Version Not Supported";
		default:  return "Unknown";
	}
}

std::string	HttpResponse::defaultErrorBody(int code)
{
	std::ostringstream oss;
	oss << "<!DOCTYPE html><html><head><title>"
		<< code << " " << reasonPhrase(code)
		<< "</title></head><body style=\"font-family:sans-serif;text-align:center;\"><h1>"
		<< code << " " << reasonPhrase(code)
		<< "</h1><hr><p>webserv</p></body></html>";
	return oss.str();
}
