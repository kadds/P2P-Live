#include "net/p2p/tracker.hpp"
#include "net/socket.hpp"

namespace net::p2p
{

void tracker_server_t::bind(event_context_t &context, socket_addr_t addr, bool reuse_addr)
{
    server.at_client_join([this](tcp::server_t &server, tcp::connection_t conn) {

    });
    server.listen(context, addr, 10000000, reuse_addr);
}

tracker_server_t &tracker_server_t::at_tracker_ping(tracker_ping_handler_t handler)
{
    this->ping_handler = handler;
    return *this;
}

tracker_server_t &tracker_server_t::at_client_join(tracker_client_join_handler_t handler)
{
    this->client_handler = handler;
    return *this;
}

tracker_node_client_t &tracker_node_client_t::at_nodes_update(at_nodes_update_handler_t handler)
{
    update_handler = handler;
    return *this;
}

} // namespace net::p2p
