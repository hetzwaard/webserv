#ifndef HTTPRESPONSE_HPP
# define HTTPRESPONSE_HPP

# include <string>
# include <map>

/* Build an HTTP/1.1 response and serialize it to wire bytes.
 *
 * Headers map is case-sensitive as stored; use canonical casing on insert
 * ("Content-Type", "Content-Length", etc.) to match what clients expect.
 * Content-Length, Connection and Server headers are auto-filled if unset.
 */
class HttpResponse
{
public:
	HttpResponse();
	explicit HttpResponse(int status);

	void	setStatus(int code);
	void	setHeader(const std::string& name, const std::string& value);
	void	setBody(const std::string& body);

	int					status() const;
	const std::string&	body() const;

	std::string			serialize() const;

	static std::string	reasonPhrase(int code);
	static std::string	defaultErrorBody(int code);

private:
	typedef std::map<std::string, std::string> HeaderMap;

	int			_status;
	std::string	_body;
	HeaderMap	_headers;
};

#endif
