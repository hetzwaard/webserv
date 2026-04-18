#ifndef CONNECTION_HPP
# define CONNECTION_HPP

# include <string>
# include <ctime>
# include <cstddef>

class Connection {
public:
	enum State {
		READING_REQUEST,
		WRITING_RESPONSE
	};

	Connection();
	explicit Connection(int fd);

	int				fd() const;
	State			state() const;
	void			setState(State s);

	std::string&		readBuf();
	const std::string&	readBuf() const;

	std::string&		writeBuf();
	const std::string&	writeBuf() const;

	std::size_t		bytesSent() const;
	void			addBytesSent(std::size_t n);
	void			resetBytesSent();

	std::time_t		lastActivity() const;
	void			touch();

	bool			writeComplete() const;

private:
	int				_fd;
	State			_state;
	std::string		_readBuf;
	std::string		_writeBuf;
	std::size_t		_bytesSent;
	std::time_t		_lastActivity;
};

#endif
