#include "net/socket_addr.hpp"
#include <memory>
#include <sstream>
#include <string>
namespace net
{
socket_addr_t::socket_addr_t()
    : so_addr({})
{
    so_addr.sin_zero[0] = 1;
}

socket_addr_t::socket_addr_t(int port)
    : so_addr({})
{
    so_addr.sin_family = AF_INET;
    so_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    so_addr.sin_port = htons(port);
}

socket_addr_t::socket_addr_t(u32 addr, int port)
    : so_addr({})
{
    so_addr.sin_family = AF_INET;
    so_addr.sin_addr.s_addr = htonl(addr);
    so_addr.sin_port = htons(port);
}

socket_addr_t::socket_addr_t(sockaddr_in addr)
    : so_addr(addr)
{
}

socket_addr_t::socket_addr_t(std::string addr, int port)
    : so_addr({})
{
    so_addr.sin_family = AF_INET;
    so_addr.sin_addr.s_addr = inet_addr(addr.c_str());
    so_addr.sin_port = htons(port);
}

std::string socket_addr_t::get_addr() const
{
    if (so_addr.sin_zero[0])
        return "invalid address";
    return inet_ntoa(so_addr.sin_addr);
}

std::string socket_addr_t::to_string() const
{
    std::stringstream ss;
    ss << get_addr() << ":" << get_port();
    return ss.str();
}

u32 socket_addr_t::v4_addr() const { return ntohl(so_addr.sin_addr.s_addr); }

int socket_addr_t::get_port() const { return ntohs(so_addr.sin_port); }

} // namespace net