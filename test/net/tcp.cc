#include "net/tcp.hpp"
#include "net/co.hpp"
#include "net/event.hpp"
#include "net/socket.hpp"
#include "net/socket_buffer.hpp"
#include <functional>
#include <gtest/gtest.h>
using namespace net;
static std::string test_data = "test string";
TEST(TCPTest, TCPTestServerClient)
{
    socket_addr_t test_addr("127.0.0.1", 2222);
    event_context_t ctx(event_strategy::epoll);
    event_loop_t loop;
    ctx.add_event_loop(&loop);
    tcp::server_t server;

    server.at_client_join([](tcp::server_t &s, socket_t *socket) {
        socket_buffer_t buffer(test_data.size());
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(socket_aread, socket, buffer), io_result::ok);

        GTEST_ASSERT_EQ(buffer.to_string(), test_data);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(socket_awrite, socket, buffer), io_result::ok);
    });
    server.listen(ctx, test_addr, 1, true);

    tcp::client_t client;
    client
        .at_server_connect([](tcp::client_t &c, socket_t *socket) {
            socket_buffer_t buffer(test_data);
            buffer.expect().origin_length();
            GTEST_ASSERT_EQ(co::await(socket_awrite, socket, buffer), io_result::ok);
            buffer.expect().origin_length();
            GTEST_ASSERT_EQ(co::await(socket_aread, socket, buffer), io_result::ok);
            GTEST_ASSERT_EQ(buffer.to_string(), test_data);
        })
        .at_server_disconnect([&loop](tcp::client_t &c, socket_t *socket) { loop.exit(0); });

    client.connect(ctx, test_addr, net::make_timespan(1));

    loop.run();
    server.close_server();
}

TEST(TCPTest, TCPTimeout)
{
    socket_addr_t test_addr("8.8.8.8", 2222);
    event_context_t ctx(event_strategy::epoll);
    event_loop_t loop;
    ctx.add_event_loop(&loop);

    tcp::client_t client;
    client.at_server_connection_error([&loop](tcp::client_t &c, socket_t *socket, connection_state state) {
        GTEST_ASSERT_EQ((int)state, (int)connection_state::timeout);
        loop.exit(0);
    });

    client.connect(ctx, test_addr, net::make_timespan(1));
    loop.add_timer(make_timer(make_timespan(1, 500, 0), [&loop]() {
        loop.exit(-1);
        std::string str = "timeout test failed";
        GTEST_ASSERT_EQ(str, "");
    }));
    loop.run();
}