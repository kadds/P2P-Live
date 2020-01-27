#include "net/event.hpp"
#include "net/net.hpp"
#include <iostream>

int server_fd;

void on_client_readable(net::event_base_t &base, net::event_t event);
void on_client_writable(net::event_base_t &base, net::event_t event);
void on_client_error(net::event_base_t &base, net::event_t event);

void on_client_exit(net::event_base_t &base, net::event_t event)
{
    std::cout << "client exit " << event.socket.remote_addr().to_string() << "\n";
}

void on_client_join(net::event_base_t &base, net::event_t event)
{
    auto client_socket = net::accept_from(event.socket);
    std::cout << "client join " << client_socket.remote_addr().to_string() << "\n";
    base.add_handler(net::event_type::readable, client_socket, on_client_readable);
    base.add_handler(net::event_type::error, client_socket, on_client_error);
}

void on_client_readable(net::event_base_t &base, net::event_t event)
{
    if (event.socket.get_raw_handle() == server_fd)
    {
        on_client_join(base, event);
        return;
    }
    net::socket_buffer_t buffer(512);
    if (net::io_result::closed == event.socket.read(buffer))
    {
        on_client_exit(base, event);
        base.close_socket(event.socket);
        return;
    }
    net::socket_buffer_t echo("echo: ");

    event.socket.write(echo);
    event.socket.write(buffer);
}

void on_client_writable(net::event_base_t &base, net::event_t event) {}

void on_client_error(net::event_base_t &base, net::event_t event) { std::cout << "error socket\n"; }

int main()
{
    net::init_lib();
    auto server_socket = net::listen_from(net::socket_addr_t("0.0.0.0", net::command_port), 1000);
    server_fd = server_socket.get_raw_handle();
    net::event_base_t event_base(net::event_base_strategy::select);
    event_base.add_handler(net::event_type::readable, server_socket, on_client_readable);
    event_base.add_handler(net::event_type::error, server_socket, on_client_error);

    int code = event_base.run();
    std::cout << "exit code " << code << "\n";
    return code;
}
