#include "../include/Config.hpp"

static std::vector<std::string>	tokenize(const std::string &path)
{
	std::ifstream	file(path.c_str());
	if (!file.is_open())
		throw std::runtime_error("cannot open config file: " + path);

	std::vector<std::string>	tokens;
	std::string						current;
	char								c;

	while (file.get(c))
	{
		if (c == '{' || c == '}' || c == ';')
		{
			if (!current.empty())
			{
				tokens.push_back(current);
				current.clear();
			}
			tokens.push_back(std::string(1, c));
		}
		else if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
		{
			if (!current.empty())
			{
				tokens.push_back(current);
				current.clear();
			}
		}
		else
			current += c;
	}
	if (!current.empty())
		tokens.push_back(current);

	return (tokens);
}

// give me the current token and step the cursor forward one
static const std::string	&next(const std::vector<std::string> &t, size_t &i)
{
	if (i >= t.size())
		throw std::runtime_error("config: unexpected end of file");
	return t[i++];
}

// the grammar says the next token must be this exact word — verify it and move past it
static void	expect(const std::vector<std::string> &t, size_t &i, const std::string &tok)
{
	const std::string	&got = next(t, i);
	if (got != tok)
		throw std::runtime_error("config: expected '" + tok + "' but got '" + got + "'");
}

static Location	parseLocation(const std::vector<std::string> &t, size_t &i)
{
	Location	loc;
	loc.path = next(t, i);
	expect(t, i, "{");

	while (i < t.size() && t[i] != "}")
	{
		const std::string	&key = next(t, i);

		if (key == "root")
		{
			loc.root = next(t, i);
			expect(t, i, ";");
		}
		else if (key == "index")
		{
			loc.index = next(t, i);
			expect(t, i, ";");
		}
		else if (key == "upload_dir")
		{
			loc.uploadDir = next(t, i);
			expect(t, i, ";");
		}
		else if (key == "cgi")
		{
			std::string	ext = next(t, i);
			std::string	bin = next(t, i);
			loc.cgi[ext] = bin;
			expect(t, i, ";");
		}
		else if (key == "autoindex")
		{
			loc.autoindex = (next(t, i) == "on");
			expect(t, i, ";");
		}
		else if (key == "methods")
		{
			while (i < t.size() && t[i] != ";")
				loc.methods.push_back(next(t, i));
			expect(t, i, ";");
		}
		else if (key == "return")
		{
			loc.redirect = next(t, i);
			expect(t, i, ";");
		}
		else
			throw std::runtime_error("config: unknown location directive '" + key + "'");
	}
	expect(t, i, "}");
	return (loc);
}

static ServerConfig	parseServer(const std::vector<std::string> &t, size_t &i)
{
	ServerConfig	s;
	expect(t, i, "{");

	while (i < t.size() && t[i] != "}")
	{
		const std::string &key = next(t, i);

		if (key == "host")
		{
			s.host = next(t, i);
			expect(t, i, ";");
		}
		else if (key == "port")
		{
			s.port = std::atoi(next(t, i).c_str());
			expect(t, i, ";");
		}
		else if (key == "server_name")
		{
			s.serverName = next(t, i);	// stored, but we do not route on it
			expect(t, i, ";");
		}
		else if (key == "client_max_body_size")
		{
			s.maxBodySize = std::atoi(next(t, i).c_str());
			expect(t, i, ";");
		}
		else if (key == "error_page")
		{
			int code = std::atoi(next(t, i).c_str());
			s.errorPages[code] = next(t, i);
			expect(t, i, ";");
		}
		else if (key == "location")
			s.locations.push_back(parseLocation(t, i));
		else
			throw std::runtime_error("config: unknown server directive '" + key + "'");
	}
	expect(t, i, "}");
	return (s);
}

std::vector<ServerConfig>	parseConfig(const std::string &path)
{
	std::vector<std::string>	tokens = tokenize(path);
	std::vector<ServerConfig>	servers;
	size_t							i = 0;

	while (i < tokens.size())
	{
		expect(tokens, i, "server");
		servers.push_back(parseServer(tokens, i));
	}

	if (servers.empty())
		throw std::runtime_error("config: no server blocks found");

	return (servers);
}
