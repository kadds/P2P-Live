#pragma once
#include <exception>
#include <string>
namespace net
{
class net_connect_exception : public std::exception
{
    std::string str;

  public:
    net_connect_exception(std::string str)
        : str(str)
    {
    }

    const char *what() { return str.c_str(); }
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