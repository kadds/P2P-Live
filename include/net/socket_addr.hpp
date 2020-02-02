#pragma once
#include "net.hpp"
#include <string>
namespace net
{
class socket_addr_t
{
    sockaddr_in so_addr;

  public:
    socket_addr_t();
    /// init address (any:port)
    socket_addr_t(int port);
    socket_addr_t(sockaddr_in addr);
    socket_addr_t(std::string addr, int port);
    std::string to_string();
    std::string get_addr();
    int get_port();

    sockaddr_in get_raw_addr() { return so_addr; }

    bool operator==(const socket_addr_t &addr) const
    {
        return addr.so_addr.sin_port == so_addr.sin_port && addr.so_addr.sin_family == so_addr.sin_family &&
               addr.so_addr.sin_addr.s_addr == so_addr.sin_addr.s_addr;
    }

    bool operator!=(const socket_addr_t &addr) const { return !operator==(addr); }
};
}; // namespace net