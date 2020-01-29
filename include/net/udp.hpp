#pragma once
#include "event.hpp"
#include "socket_addr.hpp"

namespace net
{
class socket_t;
};

namespace net::udp
{

class client
{
    socket_t *target;

  public:
    void connect(socket_addr_t addr);
};

} // namespace net::udp
