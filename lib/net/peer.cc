#include "net/peer.hpp"
#include "net/co.hpp"
#include "net/endian.hpp"
#include "net/load_balance.hpp"
#include "net/socket.hpp"
#include "net/socket_buffer.hpp"
namespace net::peer
{
/// client -------------------------------------------------------------------------------------
peer_client_t::peer_client_t()
    : in_peer_network(false)
{
}

peer_client_t::~peer_client_t() {}

void peer_client_t::front_server_main(event_context_t &context, tcp::client_t &client, socket_t *socket)
{
    peer_get_server_request_t request;
    peer_get_server_respond_t respond;
    request.room_id = room_id;
    request.version = 1;
    /// TODO: gen key
    request.key = get_current_time() % 1024;
    socket_buffer_t send_buffer(sizeof(request));
    send_buffer.expect().origin_length();
    assert(endian::save_to(request, send_buffer));
    co::await(socket_awrite, socket, send_buffer);
    socket_buffer_t recv_buffer(sizeof(respond));
    recv_buffer.expect().origin_length();
    co::await(socket_aread, socket, recv_buffer);
    assert(endian::cast_to(recv_buffer, respond));
    // we get content server address
    if (respond.state == 0)
    {
        socket_addr_t content_server_addr(respond.ip_addr, respond.port);
        room_client.at_server_connect([this](tcp::client_t &client, socket_t *socket) {
            if (handler)
            {
                handler(*this, socket);
            }
        });
        room_client.connect(context, content_server_addr);
    }
    else
    {
        if (error_handler)
            error_handler(*this, socket);
    }
}

void peer_client_t::join_peer_network(event_context_t &context, socket_addr_t server_addr, int room_id)
{
    this->room_id = room_id;
    client
        .at_server_connect(std::bind(&peer_client_t::front_server_main, this, std::ref(context), std::placeholders::_1,
                                     std::placeholders::_2))
        .at_server_connection_error([this](tcp::client_t &client, socket_t *socket) {
            if (error_handler)
                error_handler(*this, socket);
        });

    client.connect(context, server_addr);
}

peer_client_t &peer_client_t::at_connnet_peer_server(connect_peer_server_handler_t handler)
{
    this->handler = handler;
    return *this;
}

peer_client_t &peer_client_t::at_connect_peer_server_error(connect_peer_server_handler_t handler)
{
    this->error_handler = handler;
    return *this;
}

/// server-------------------------------------------------------------------------------------------

void peer_server_t::connect_to_front_server(event_context_t &context, socket_addr_t addr)
{
    client
        .at_server_connect([this](tcp::client_t &client, socket_t *socket) {
            if (front_server_handler)
            {
                front_server_handler(true, socket);
            }
        })
        .at_server_connection_error([this](tcp::client_t &client, socket_t *socket) {
            if (front_server_handler)
            {
                front_server_handler(false, socket);
            }
        });
    client.connect(context, addr);
}

void peer_server_t::bind_server(event_context_t &context, socket_addr_t bind_taddr, bool reuse_addr)
{
    server.at_client_join([this](tcp::server_t &server, socket_t *socket) {
        if (client_handler)
            client_handler(*this, socket);
    });

    server.listen(context, bind_taddr, 100000, reuse_addr);
}

peer_server_t &peer_server_t::at_client_join(client_join_handler_t handler)
{
    client_handler = handler;
    return *this;
}

peer_server_t &peer_server_t::at_front_server_connect(front_server_connect_handler_t handler)
{
    front_server_handler = handler;
    return *this;
}

} // namespace net::peer