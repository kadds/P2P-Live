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
    net::event_loop_t looper;
    app_context->add_event_loop(&looper);
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
    server.listen(context, net::socket_addr_t("0.0.0.0", net::command_port), 1000);

    server.at_client_join([](net::tcp::server_t &server, net::socket_t *socket) {
        std::cout << "client join " << socket->remote_addr().to_string() << "\n";
    });

    server.at_client_exit([](net::tcp::server_t &server, net::socket_t *socket) {
        std::cout << "client exit " << socket->remote_addr().to_string() << "\n";
    });

    return looper.run();
}
