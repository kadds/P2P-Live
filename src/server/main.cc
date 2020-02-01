#include "net/event.hpp"
#include "net/net.hpp"
#include "net/socket.hpp"
#include "net/tcp.hpp"
#include "net/udp.hpp"
#include <iostream>
#include <thread>

net::event_context_t *app_context;

void thread_main()
{
    net::event_context_t context(net::event_strategy::epoll);
    net::event_loop_t looper;
    context.add_event_loop(&looper);
    net::tcp::client_t client;
    client
        .at_server_connect([](net::tcp::client_t &client, net::socket_t *socket) {
            std::cout << "server connection ok. " << client.get_connect_addr().to_string() << std::endl;
            net::socket_buffer_t read_data(100);

            net::socket_buffer_t buf("hi, world");
            while (1)
            {
                buf.expect().origin_length();
                net::co::await(std::bind(&net::socket_t::awrite, socket, std::placeholders::_1), buf);
                read_data.expect().origin_length();
                net::co::await(std::bind(&net::socket_t::aread, socket, std::placeholders::_1), read_data);
            }
        })
        .at_server_connection_error([](net::tcp::client_t &client, net::socket_t *socket) {
            std::cerr << "server connection failed! to " << client.get_connect_addr().to_string() << std::endl;
        })
        .at_server_disconnect([](net::tcp::client_t &client, net::socket_t *socket) {
            std::cout << "server connection closed! " << client.get_connect_addr().to_string() << std::endl;
        });

    client.connect(context, net::socket_addr_t("127.0.0.1", 1233));

    looper.run();
}

int main()
{
    net::init_lib();
    net::event_context_t context(net::event_strategy::epoll);
    app_context = &context;

    net::event_loop_t looper;
    app_context->add_event_loop(&looper);

    std::thread thd(thread_main);
    thd.detach();

    net::tcp::server_t server;

    server
        .at_client_join([](net::tcp::server_t &server, net::socket_t *socket) {
            std::cout << "client join " << socket->remote_addr().to_string() << "\n";
            net::socket_buffer_t buffer(20);
            net::socket_buffer_t echo("echo:");

            while (1)
            {
                buffer.expect().origin_length();
                net::co::await(std::bind(&net::socket_t::aread, socket, std::placeholders::_1), buffer);
                buffer.expect().origin_length();
                echo.expect().origin_length();
                net::co::await(std::bind(&net::socket_t::awrite, socket, std::placeholders::_1), echo);
                net::co::await(std::bind(&net::socket_t::awrite, socket, std::placeholders::_1), buffer);
            }
        })
        .at_client_exit([](net::tcp::server_t &server, net::socket_t *socket) {
            std::cout << "client exit " << socket->remote_addr().to_string() << "\n";
        });
    server.listen(context, net::socket_addr_t("127.0.0.1", net::command_port), 1000);

    return looper.run();
}
