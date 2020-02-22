#pragma once
#include <exception>
#include <string>
namespace net
{
enum class connection_state
{
    closed,
    close_by_peer,
    connection_refuse,
    address_in_used,
    no_resource,
    timeout,
    secure_check_failed,
};

class net_connect_exception : public std::exception
{
    std::string str;
    connection_state state;

  public:
    net_connect_exception(std::string str, connection_state state)
        : str(str)
    {
    }

    const char *what() { return str.c_str(); }

    connection_state get_state() const { return state; }
};

class net_param_exception : public std::exception
{
    std::string str;

  public:
    net_param_exception(std::string str)
        : str(str)
    {
    }

    const char *what() { return str.c_str(); }
};

class net_io_exception : public std::exception
{
    std::string str;

  public:
    net_io_exception(std::string str)
        : str(str)
    {
    }

    const char *what() { return str.c_str(); }
};

} // namespace net