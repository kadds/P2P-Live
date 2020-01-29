#include "net/tcp.hpp"
#include "net/socket.hpp"

namespace net::tcp
{
void server_t::listen(event_context_t &context, socket_addr_t address, int max_client)
{
    this->context = &context;
    server_socket = listen_from(address, max_client);

    context.add_socket(server_socket).link(server_socket, net::event_type::readable | net::event_type::error);
}

void server_t::close() { close_socket(server_socket); }

void server_t::at_client_join(handler_t handler) { join_handler = handler; }

void server_t::at_client_exit(handler_t handler) { exit_handler = handler; }

void client_t::connect(event_context_t &context, socket_addr_t address) { socket = connect_to(address); }

void client_t::close(event_context_t &context) { close_socket(socket); }

} // namespace net::tcp