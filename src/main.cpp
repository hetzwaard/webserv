#include "server/Server.hpp"

int main(void)
{
	try
	{
		Server s;
		s.addListener(9090);
		s.addListener(9091);
		s.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "fatal: " << e.what() << std::endl;
		return (1);
	}
	return (0);a
}
