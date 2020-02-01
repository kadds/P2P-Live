#include "net/udp.hpp"
#include "net/socket.hpp"
namespace net::udp
{
socket_t *server_t::bind(event_context_t &context, socket_addr_t addr)
{
    this->context = &context;
    socket = new_udp_socket();
    context.add_socket(socket);
    bind_at(socket, addr);
    return socket;
}

server_t &server_t::at_client_join(handler_t handler)
{
    join_handler = handler;
    return *this;
}

server_t &server_t::at_client_exit(handler_t handler)
{
    exit_handler = handler;
    return *this;
}

server_t &server_t::at_client_error(handler_t handler)
{
    error_handler = handler;
    return *this;
}

void server_t::exit_client(client_t &client) { client.close(); }

void server_t::close() {}

void client_t::co_main() {}

void client_t::connect(event_context_t &context, socket_addr_t addr, bool remote_address_bind_to_socket)
{
    this->context = &context;
    connect_addr = addr;
    socket = new_udp_socket();
    context.add_socket(socket);
    if (remote_address_bind_to_socket)
    {
        connect_udp(socket, addr, 0);
    }
}

socket_addr_t client_t::get_address() const { return connect_addr; }

void client_t::close()
{
    context->remove_socket(socket);
    co::coroutine_t::yield([this]() { close_socket(socket); });
}

} // namespace net::udp
