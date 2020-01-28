#include "net/event.hpp"
#include "net/net.hpp"
#include "net/socket.hpp"
#include <iostream>

net::socket_t *server_socket;
net::event_context_t *app_context;

net::event_result on_event(net::event_context_t &ctx, net::event_type_t type, net::socket_t *socket);

net::event_result on_client_readable(net::event_context_t &, net::event_type_t, net::socket_t *);
net::event_result on_client_writable(net::event_context_t &, net::event_type_t, net::socket_t *);
net::event_result on_client_error(net::event_context_t &, net::event_type_t, net::socket_t *);

net::event_result on_client_exit(net::event_context_t &ctx, net::event_type_t type, net::socket_t *socket)
{
    std::cout << "client exit " << socket->remote_addr().to_string() << "\n";
    ctx.remove_socket(socket);
    return net::event_result::ok;
}

net::event_result on_client_join(net::event_context_t &ctx, net::event_type_t type, net::socket_t *socket)
{
    auto client_socket = net::accept_from(socket);
    std::cout << "client join " << client_socket->remote_addr().to_string() << "\n";
    client_socket->add_handler(on_event);
    ctx.add_socket(client_socket)
        .link(client_socket, net::event_type::readable)
        .link(client_socket, net::event_type::error);
    return net::event_result::ok;
}

net::event_result on_client_readable(net::event_context_t &ctx, net::event_type_t type, net::socket_t *socket)
{
    if (socket == server_socket)
    {
        return on_client_join(ctx, type, socket);
    }
    net::socket_buffer_t buffer(512);
    if (net::io_result::closed == socket->read(buffer))
    {
        return on_client_exit(ctx, type, socket);
    }
    net::socket_buffer_t echo("echo: ");

    socket->write(echo);
    socket->write(buffer);
    return net::event_result::ok;
}

net::event_result on_client_writable(net::event_context_t &, net::event_type_t, net::socket_t *)
{
    return net::event_result::ok;
}

net::event_result on_event(net::event_context_t &ctx, net::event_type_t type, net::socket_t *socket)
{
    switch (type)
    {
        case net::event_type::writable:
            return on_client_writable(ctx, type, socket);
        case net::event_type::readable:
            return on_client_readable(ctx, type, socket);
        case net::event_type::error:
            return on_client_error(ctx, type, socket);
        default:
            return net::event_result::remove_handler;
    }
}

net::event_result on_client_error(net::event_context_t &ctx, net::event_type_t type, net::socket_t *socket)
{
    std::cout << "error socket\n";
    return net::event_result::ok;
}

int main()
{
    net::init_lib();
    net::event_context_t context(net::event_strategy::epoll);
    app_context = &context;

    server_socket = net::listen_from(net::socket_addr_t("0.0.0.0", net::command_port), 1000);
    server_socket->add_handler(on_event);
    net::event_loop_t looper;
    app_context->add_event_loop(&looper);
    app_context->add_socket(server_socket).link(server_socket, net::event_type::readable | net::event_type::error);

    int code = looper.run();
    std::cout << "exit code " << code << "\n";
    return code;
}
