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

std::string socket_addr_t::get_addr()
{
    if (so_addr.sin_zero[0])
        return "invalid address";
    return inet_ntoa(so_addr.sin_addr);
}

std::string socket_addr_t::to_string()
{
    std::stringstream ss;
    ss << get_addr() << ":" << get_port();
    return ss.str();
}

int socket_addr_t::get_port() { return ntohs(so_addr.sin_port); }

} // namespace net