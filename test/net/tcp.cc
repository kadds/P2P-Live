#include "net/tcp.hpp"
#include "net/co.hpp"
#include "net/event.hpp"
#include "net/socket.hpp"
#include "net/socket_buffer.hpp"
#include <functional>
#include <gtest/gtest.h>

TEST(TCPTest, TCPTestInput)
{
    using namespace net;
    std::string test_data = "test string";
    socket_addr_t test_addr("127.0.0.1", 2222);
    init_lib();
    tcp::server_t server;
    event_context_t ctx(event_strategy::epoll);
    event_loop_t loop;
    ctx.add_event_loop(&loop);

    server.at_client_join([&test_data](tcp::server_t &s, socket_t *socket) {
        socket_buffer_t buffer(test_data.size());
        buffer.expect().origin_length();
        co::await(std::bind(&socket_t::aread, socket, std::placeholders::_1), buffer);
        std::string str((char *)buffer.get(), buffer.get_buffer_length());

        GTEST_ASSERT_EQ((const char *)buffer.get(), test_data);
        buffer.expect().origin_length();
        co::await(std::bind(&socket_t::awrite, socket, std::placeholders::_1), buffer);
    });
    server.listen(ctx, test_addr, 1);

    tcp::client_t client;
    client
        .at_server_connect([&test_data](tcp::client_t &c, socket_t *socket) {
            socket_buffer_t buffer(test_data);
            buffer.expect().origin_length();
            co::await(std::bind(&socket_t::awrite, socket, std::placeholders::_1), buffer);
            buffer.expect().origin_length();
            co::await(std::bind(&socket_t::aread, socket, std::placeholders::_1), buffer);
            GTEST_ASSERT_EQ((const char *)buffer.get(), test_data);
        })
        .at_server_disconnect([&loop](tcp::client_t &c, socket_t *socket) { loop.exit(0); });

    client.connect(ctx, test_addr);

    loop.run();
}
