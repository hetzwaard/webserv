#ifndef HTTPREQUEST_HPP
# define HTTPREQUEST_HPP

# include <string>
# include <map>
# include <cstddef>

/* Incremental HTTP/1.1 request parser.
 *
 * Feed it bytes as they arrive (partial reads are fine). Query state() /
 * isComplete() / isError() after each feed().
 *
 * Supports: GET / POST / DELETE, Content-Length bodies, Transfer-Encoding:
 * chunked (un-chunked into body() automatically, as the subject requires for
 * CGI). Enforces a Host header on HTTP/1.1.
 *
 * Errors map to HTTP status codes via errorCode(): 400 / 413 / 414 / 431 /
 * 501 / 505. Never throws. Never crashes on malformed input.
 */
class HttpRequest
{
public:
	enum ParseState
	{
		START_LINE,
		HEADERS,
		BODY_LENGTH,
		BODY_CHUNKED,
		COMPLETE,
		ERROR_STATE
	};

	typedef std::map<std::string, std::string> HeaderMap;

	HttpRequest();

	void			feed(const char* data, std::size_t len);
	void			reset();

	ParseState		state() const;
	bool			isComplete() const;
	bool			isError() const;
	int				errorCode() const;

	const std::string&	method() const;
	const std::string&	uri() const;
	const std::string&	version() const;
	const HeaderMap&	headers() const;
	const std::string&	body() const;

	std::string		header(const std::string& lowercaseName) const;

	void			setMaxBodySize(std::size_t n);

private:
	bool	advance();
	bool	parseStartLine();
	bool	parseHeaders();
	bool	parseBodyLength();
	bool	parseBodyChunked();

	bool	extractLine(std::string& outLine);
	void	fail(int code);

	enum ChunkStep
	{
		CHUNK_SIZE,
		CHUNK_DATA,
		CHUNK_CRLF,
		CHUNK_TRAILER
	};

	ParseState	_state;
	int			_errorCode;

	std::string	_raw;

	std::string	_method;
	std::string	_uri;
	std::string	_version;
	HeaderMap	_headers;
	std::string	_body;

	std::size_t	_contentLength;
	std::size_t	_maxBodySize;

	ChunkStep	_chunkStep;
	std::size_t	_chunkRemaining;

	static const std::size_t	MAX_REQUEST_LINE = 8192;
	static const std::size_t	MAX_HEADERS      = 8192;
	static const std::size_t	DEFAULT_MAX_BODY = 1024 * 1024;
};

#endif
