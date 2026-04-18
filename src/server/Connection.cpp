#include "server/Connection.hpp"

Connection::Connection() : _fd(-1), _state(READING_REQUEST), _bytesSent(0), _lastActivity(std::time(NULL)) {}

Connection::Connection(int fd) : _fd(fd), _state(READING_REQUEST), _bytesSent(0), _lastActivity(std::time(NULL)) {}

int					Connection::fd() const		{ return _fd; }
Connection::State	Connection::state() const	{ return _state; }
void				Connection::setState(State s) { _state = s; }

std::string&		Connection::readBuf()		{ return _readBuf; }
const std::string&	Connection::readBuf() const	{ return _readBuf; }
std::string&		Connection::writeBuf()		{ return _writeBuf; }
const std::string&	Connection::writeBuf() const { return _writeBuf; }

std::size_t	Connection::bytesSent() const		{ return _bytesSent; }
void		Connection::addBytesSent(std::size_t n) { _bytesSent += n; }
void		Connection::resetBytesSent()		{ _bytesSent = 0; }

std::time_t	Connection::lastActivity() const	{ return _lastActivity; }
void		Connection::touch()					{ _lastActivity = std::time(NULL); }

bool		Connection::writeComplete() const	{ return _bytesSent >= _writeBuf.size(); }
