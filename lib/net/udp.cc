#include "net/udp.hpp"
#include "net/socket.hpp"
namespace net::udp
{
socket_t *server_t::listen(socket_addr_t addr)
{
    socket = new_udp_socket();
    bind_at(socket, addr);
    return socket;
}

} // namespace net::udp
