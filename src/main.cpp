#include "../include/Config.hpp"
#include "../include/Server.hpp"

int	main(int argc, char **argv)
{
	signal(SIGPIPE, SIG_IGN);

	std::string	path = (argc > 1) ? argv[1] : "default.conf";
	try
	{
		std::vector<ServerConfig> servers = parseConfig(path);
		Server server(servers);
		server.run();
	}
	catch(const std::exception& e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return (1);
	}
	return (0);
}
