#include "http/HttpRequest.hpp"

namespace
{
	std::string	toLower(std::string s)
	{
		for (std::size_t i = 0; i < s.size(); ++i)
			if (s[i] >= 'A' && s[i] <= 'Z')
				s[i] = static_cast<char>(s[i] + 32);
		return s;
	}

	std::string	trim(const std::string& s)
	{
		std::size_t a = 0;
		std::size_t b = s.size();
		while (a < b && (s[a] == ' ' || s[a] == '\t'))
			++a;
		while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
			--b;
		return s.substr(a, b - a);
	}

	bool	isSupportedMethod(const std::string& m)
	{
		return m == "GET" || m == "POST" || m == "DELETE";
	}

	bool	isKnownMethod(const std::string& m)
	{
		return m == "GET" || m == "POST" || m == "DELETE"
			|| m == "HEAD" || m == "PUT" || m == "OPTIONS"
			|| m == "PATCH" || m == "TRACE" || m == "CONNECT";
	}

	bool	parseHex(const std::string& s, std::size_t& out)
	{
		if (s.empty())
			return false;
		out = 0;
		for (std::size_t i = 0; i < s.size(); ++i)
		{
			char c = s[i];
			int	 d;
			if (c >= '0' && c <= '9')		d = c - '0';
			else if (c >= 'a' && c <= 'f')	d = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')	d = c - 'A' + 10;
			else							return false;
			out = out * 16 + static_cast<std::size_t>(d);
		}
		return true;
	}
}

HttpRequest::HttpRequest()
	: _state(START_LINE),
	  _errorCode(0),
	  _contentLength(0),
	  _maxBodySize(DEFAULT_MAX_BODY),
	  _chunkStep(CHUNK_SIZE),
	  _chunkRemaining(0) {}

void	HttpRequest::reset()
{
	_state = START_LINE;
	_errorCode = 0;
	_raw.clear();
	_method.clear();
	_uri.clear();
	_version.clear();
	_headers.clear();
	_body.clear();
	_contentLength = 0;
	_chunkStep = CHUNK_SIZE;
	_chunkRemaining = 0;
}

void	HttpRequest::feed(const char* data, std::size_t len)
{
	if (_state == COMPLETE || _state == ERROR_STATE)
		return;
	_raw.append(data, len);
	while (advance())
		;
}

bool	HttpRequest::advance()
{
	switch (_state)
	{
		case START_LINE:	return parseStartLine();
		case HEADERS:		return parseHeaders();
		case BODY_LENGTH:	return parseBodyLength();
		case BODY_CHUNKED:	return parseBodyChunked();
		default:			return false;
	}
}

bool	HttpRequest::extractLine(std::string& outLine)
{
	std::size_t pos = _raw.find("\r\n");
	if (pos == std::string::npos)
		return false;
	outLine = _raw.substr(0, pos);
	_raw.erase(0, pos + 2);
	return true;
}

void	HttpRequest::fail(int code)
{
	_state = ERROR_STATE;
	_errorCode = code;
}

bool	HttpRequest::parseStartLine()
{
	std::string line;
	if (!extractLine(line))
	{
		if (_raw.size() > MAX_REQUEST_LINE)
			fail(414);
		return false;
	}

	std::size_t s1 = line.find(' ');
	if (s1 == std::string::npos) { fail(400); return false; }
	std::size_t s2 = line.find(' ', s1 + 1);
	if (s2 == std::string::npos) { fail(400); return false; }

	_method  = line.substr(0, s1);
	_uri     = line.substr(s1 + 1, s2 - s1 - 1);
	_version = line.substr(s2 + 1);

	if (_method.empty() || _uri.empty() || _version.empty())
		{ fail(400); return false; }
	if (!isKnownMethod(_method))
		{ fail(400); return false; }
	if (!isSupportedMethod(_method))
		{ fail(501); return false; }
	if (_version != "HTTP/1.1" && _version != "HTTP/1.0")
		{ fail(505); return false; }
	if (_uri.size() > MAX_REQUEST_LINE)
		{ fail(414); return false; }

	_state = HEADERS;
	return true;
}

bool	HttpRequest::parseHeaders()
{
	std::string line;
	if (!extractLine(line))
	{
		if (_raw.size() > MAX_HEADERS)
			fail(431);
		return false;
	}

	if (line.empty())
	{
		if (_version == "HTTP/1.1" && _headers.find("host") == _headers.end())
			{ fail(400); return false; }

		HeaderMap::const_iterator cl = _headers.find("content-length");
		HeaderMap::const_iterator te = _headers.find("transfer-encoding");
		bool hasCl = (cl != _headers.end());
		bool hasTe = (te != _headers.end());

		if (hasCl && hasTe)
			{ fail(400); return false; }
		if (hasTe)
		{
			if (toLower(te->second) != "chunked")
				{ fail(501); return false; }
			_state = BODY_CHUNKED;
			_chunkStep = CHUNK_SIZE;
			_chunkRemaining = 0;
			return true;
		}
		if (hasCl)
		{
			const std::string& v = cl->second;
			if (v.empty()) { fail(400); return false; }
			std::size_t cv = 0;
			for (std::size_t i = 0; i < v.size(); ++i)
			{
				if (v[i] < '0' || v[i] > '9') { fail(400); return false; }
				cv = cv * 10 + static_cast<std::size_t>(v[i] - '0');
				if (cv > _maxBodySize) { fail(413); return false; }
			}
			_contentLength = cv;
			if (_contentLength == 0) { _state = COMPLETE; return false; }
			_state = BODY_LENGTH;
			return true;
		}
		_state = COMPLETE;
		return false;
	}

	std::size_t colon = line.find(':');
	if (colon == std::string::npos) { fail(400); return false; }

	std::string name  = toLower(line.substr(0, colon));
	std::string value = trim(line.substr(colon + 1));

	if (name.empty()) { fail(400); return false; }
	for (std::size_t i = 0; i < name.size(); ++i)
	{
		if (name[i] == ' ' || name[i] == '\t') { fail(400); return false; }
	}

	if (name == "content-length" && _headers.find("content-length") != _headers.end())
		{ fail(400); return false; }

	_headers[name] = value;
	return true;
}

bool	HttpRequest::parseBodyLength()
{
	std::size_t need = _contentLength - _body.size();
	if (_raw.size() >= need)
	{
		_body.append(_raw, 0, need);
		_raw.erase(0, need);
		_state = COMPLETE;
		return false;
	}
	_body.append(_raw);
	_raw.clear();
	return false;
}

bool	HttpRequest::parseBodyChunked()
{
	switch (_chunkStep)
	{
		case CHUNK_SIZE:
		{
			std::string line;
			if (!extractLine(line))
				return false;
			std::size_t semi = line.find(';');
			if (semi != std::string::npos)
				line.erase(semi);
			line = trim(line);
			std::size_t sz = 0;
			if (!parseHex(line, sz)) { fail(400); return false; }
			_chunkRemaining = sz;
			if (sz == 0)
				_chunkStep = CHUNK_TRAILER;
			else
			{
				if (_body.size() + sz > _maxBodySize) { fail(413); return false; }
				_chunkStep = CHUNK_DATA;
			}
			return true;
		}
		case CHUNK_DATA:
		{
			std::size_t take = _chunkRemaining < _raw.size() ? _chunkRemaining : _raw.size();
			if (take == 0)
				return false;
			_body.append(_raw, 0, take);
			_raw.erase(0, take);
			_chunkRemaining -= take;
			if (_chunkRemaining == 0)
				_chunkStep = CHUNK_CRLF;
			return true;
		}
		case CHUNK_CRLF:
		{
			if (_raw.size() < 2)
				return false;
			if (_raw[0] != '\r' || _raw[1] != '\n') { fail(400); return false; }
			_raw.erase(0, 2);
			_chunkStep = CHUNK_SIZE;
			return true;
		}
		case CHUNK_TRAILER:
		{
			std::string line;
			if (!extractLine(line))
				return false;
			if (line.empty())
			{
				_state = COMPLETE;
				return false;
			}
			return true;
		}
	}
	return false;
}

HttpRequest::ParseState	HttpRequest::state() const		{ return _state; }
bool					HttpRequest::isComplete() const	{ return _state == COMPLETE; }
bool					HttpRequest::isError() const	{ return _state == ERROR_STATE; }
int						HttpRequest::errorCode() const	{ return _errorCode; }

const std::string&			HttpRequest::method() const		{ return _method; }
const std::string&			HttpRequest::uri() const		{ return _uri; }
const std::string&			HttpRequest::version() const	{ return _version; }
const HttpRequest::HeaderMap&	HttpRequest::headers() const { return _headers; }
const std::string&			HttpRequest::body() const		{ return _body; }

std::string	HttpRequest::header(const std::string& lowercaseName) const
{
	HeaderMap::const_iterator it = _headers.find(lowercaseName);
	if (it == _headers.end())
		return std::string();
	return it->second;
}

void	HttpRequest::setMaxBodySize(std::size_t n) { _maxBodySize = n; }
