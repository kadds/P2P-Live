#include "net/tcp.hpp"
#include "net/socket.hpp"
#include <functional>
#include <iostream>

namespace net::tcp
{
void none_func_s(server_t &server, socket_t *socket) {}
void none_func_c(client_t &client, socket_t *socket) {}

server_t::server_t()
    : join_handler(none_func_s)
    , exit_handler(none_func_s)
    , error_handler(none_func_s)
{
}

void server_t::client_main(socket_t *socket)
{
    context->add_socket(socket);
    try
    {
        join_handler(*this, socket);
    } catch (net::net_connect_exception &e)
    {
        if (e.get_state() != connection_state::closed)
        {
            std::cerr << e.what() << '\n';
            error_handler(*this, socket);
        }
    }
    exit_client(socket);
}

void server_t::wait_client()
{
    while (1)
    {
        auto socket = co::await(accept_from, server_socket);
        auto co = co::coroutine_t::create(std::bind(&server_t::client_main, this, socket));
        socket->startup_coroutine(co);
    }
}

void server_t::listen(event_context_t &context, socket_addr_t address, int max_client)
{
    this->context = &context;
    server_socket = new_tcp_socket();
    listen_from(bind_at(server_socket, address), max_client);

    context.add_socket(server_socket).link(server_socket, net::event_type::readable);
    auto cot = co::coroutine_t::create(std::bind(&server_t::wait_client, this));
    server_socket->startup_coroutine(cot);
}

void server_t::exit_client(socket_t *client)
{
    if (client == server_socket)
        return;

    exit_handler(*this, client);
    context->remove_socket(client);
    co::coroutine_t::yield([client]() { close_socket(client); });
}

void server_t::close_server() { close_socket(server_socket); }

server_t &server_t::at_client_join(server_handler_t handler)
{
    join_handler = handler;
    return *this;
}

server_t &server_t::at_client_exit(server_handler_t handler)
{
    exit_handler = handler;
    return *this;
}

server_t &server_t::at_client_connection_error(server_handler_t handler)
{
    error_handler = handler;
    return *this;
}

client_t::client_t()
    : join_handler(none_func_c)
    , exit_handler(none_func_c)
    , error_handler(none_func_c)
{
}

void client_t::wait_server(socket_addr_t address)
{
    if (io_result::ok == co::await(connect_to, socket, address, 0))
    {
        try
        {
            join_handler(*this, socket);
        } catch (net::net_connect_exception &e)
        {
            if (e.get_state() != connection_state::closed)
            {
                std::cerr << e.what() << '\n';
                error_handler(*this, socket);
            }
        }
    }
    else
    {
        error_handler(*this, socket);
    }
    close();
}

void client_t::connect(event_context_t &context, socket_addr_t address)
{
    this->context = &context;
    socket = new_tcp_socket();
    connect_addr = address;

    context.add_socket(socket);
    auto cot = co::coroutine_t::create(std::bind(&client_t::wait_server, this, address));
    socket->startup_coroutine(cot);
}

client_t &client_t::at_server_connect(client_handler_t handler)
{
    join_handler = handler;
    return *this;
}

client_t &client_t::at_server_disconnect(client_handler_t handler)
{
    exit_handler = handler;
    return *this;
}

client_t &client_t::at_server_connection_error(client_handler_t handler)
{
    error_handler = handler;
    return *this;
}

void client_t::close()
{
    exit_handler(*this, socket);
    context->remove_socket(socket);
    co::coroutine_t::yield([this]() { close_socket(socket); });
}

} // namespace net::tcp