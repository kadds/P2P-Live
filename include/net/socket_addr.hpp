#pragma once
#include "net.hpp"
#include <string>
namespace net
{
class socket_addr_t
{
    sockaddr_in so_addr;

  public:
    socket_addr_t(sockaddr_in addr);
    socket_addr_t(std::string addr, int port);
    std::string to_string();
    std::string get_addr();
    int get_port();

    sockaddr_in get_raw_addr() { return so_addr; }
};
}; // namespace net