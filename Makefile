NAME		= webserv
CXX		= c++
CXXFLAGS	= -Wall -Wextra -Werror -std=c++98

SRCDIR	= src
OBJDIR	= obj
SRCS		= main.cpp Config.cpp Server.cpp Http.cpp Cgi.cpp
OBJS		= $(addprefix $(OBJDIR)/, $(SRCS:.cpp=.o))

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)
	rm -rf $(OBJDIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re