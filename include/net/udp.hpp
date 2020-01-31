#pragma once
#include "event.hpp"
#include "socket_addr.hpp"

namespace net
{
class socket_t;
};

namespace net::udp
{
class server_t
{
    socket_t *socket;

  public:
    socket_t *get_socket() { return socket; }
    socket_t *listen(socket_addr_t addr);
};

class client
{
    socket_t *socket;
    socket_addr_t target;

  public:
    void connect(socket_addr_t addr);

    socket_t *get_socket() { return socket; }
};

} // namespace net::udp
