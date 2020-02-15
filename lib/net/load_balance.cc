#include "net/load_balance.hpp"
#include "net/event.hpp"
#include "net/socket.hpp"

namespace net::load_balance
{

void front_server_t::bind_inner(event_context_t &context, socket_addr_t addr, bool reuse_addr)
{
    /// content server join
    inner_server.at_client_join([this](tcp::server_t &server, tcp::connection_t conn) {
        if (server_handler)
            server_handler(*this, conn);
    });
    inner_server.listen(context, addr, 100000, reuse_addr);
}

void front_server_t::bind(event_context_t &context, socket_addr_t addr, bool reuse_addr)
{
    server.at_client_join([this](tcp::server_t &server, tcp::connection_t conn) {
        peer_get_server_request_t request;
        peer_get_server_respond_t respond;
        socket_buffer_t buffer(sizeof(request));
        buffer.expect().origin_length();
        co::await(tcp::connection_aread, conn, buffer);
        assert(endian::cast_to<>(buffer, request));
        if (handler)
        {
            if (handler(*this, request, respond, conn))
            {
                socket_buffer_t respond_buffer(sizeof(respond));
                respond_buffer.expect().origin_length();
                assert(endian::save_to<>(respond, respond_buffer));
                co::await(tcp::connection_awrite, conn, respond_buffer);
            }
        }
    });
    server.listen(context, addr, 100000, reuse_addr);
}

front_server_t &front_server_t::at_client_request(front_handler_t handler)
{
    this->handler = handler;
    return *this;
}

front_server_t &front_server_t::at_inner_server_join(server_join_handler_t handler)
{
    this->server_handler = handler;
    return *this;
}

} // namespace net::load_balance
