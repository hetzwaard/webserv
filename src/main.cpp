#include "../include/Config.hpp"
#include "../include/Server.hpp"

int	main(int argc, char **argv)
{
	signal(SIGPIPE, SIG_IGN);

	if (argc > 2)
	{
		std::cerr << "Usage: ./webserv [configuration file]" << std::endl;
		return (EXIT_FAILURE);
	}

	std::string	path;

	if (argc == 2)
		path = argv[1];
	else
		path = "default.conf";
		
	try
	{
		std::vector<ServerConfig> servers = parseConfig(path);
		Server server(servers);
		server.run();
	}
	catch(const std::exception& e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return (EXIT_FAILURE);
	}

	return (EXIT_SUCCESS);
}
