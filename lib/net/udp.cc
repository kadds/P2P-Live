#include "net/udp.hpp"
#include "net/socket.hpp"
namespace net::udp
{
socket_t *server_t::bind(event_context_t &context, socket_addr_t addr, bool reuse_port)
{
    this->context = &context;
    socket = new_udp_socket();
    context.add_socket(socket);
    if (reuse_port)
        reuse_port_socket(socket, true);
    bind_at(socket, addr);
    return socket;
}

void server_t::run(std::function<void()> func) { socket->startup_coroutine(co::coroutine_t::create(func)); }

server_t::~server_t() { close(); }

void server_t::close()
{
    if (!socket)
        return;

    context->remove_socket(socket);
    if (co::coroutine_t::in_coroutine(socket->get_coroutine()))
    {
        co::coroutine_t::yield([this]() {
            close_socket(socket);
            socket = nullptr;
        });
    }
    else
    {
        close_socket(socket);
        socket = nullptr;
    }
}

client_t::~client_t() { close(); }

void client_t::connect(event_context_t &context, socket_addr_t addr, bool remote_address_bind_to_socket)
{
    this->context = &context;
    connect_addr = addr;
    socket = new_udp_socket();
    context.add_socket(socket);
    if (remote_address_bind_to_socket)
    {
        /// connect udp must return immediately.
        co::await(connect_udp, socket, addr);
    }
}

void client_t::run(std::function<void()> func) { socket->startup_coroutine(co::coroutine_t::create(func)); }

socket_addr_t client_t::get_address() const { return connect_addr; }

void client_t::close()
{
    if (!socket)
        return;

    context->remove_socket(socket);
    if (co::coroutine_t::in_coroutine(socket->get_coroutine()))
    {
        co::coroutine_t::yield([this]() {
            close_socket(socket);
            socket = nullptr;
        });
    }
    else
    {
        close_socket(socket);
        socket = nullptr;
    }
}

} // namespace net::udp
