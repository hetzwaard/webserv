NAME		= webserv

CXX		= c++
CXXFLAGS	= -Wall -Wextra -Werror -std=c++98

SRCDIR	= src
OBJDIR	= obj
INCDIR	= include

SRCS		= $(SRCDIR)/main.cpp

OBJS		= $(SRCS:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -c $< -o $@

clean:
	rm -rf $(OBJDIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re