#include "net/udp.hpp"
#include "net/co.hpp"
#include "net/event.hpp"
#include "net/socket.hpp"
#include "net/socket_buffer.hpp"
#include <functional>
#include <gtest/gtest.h>

static std::string test_data = "test string";
using namespace net;

TEST(UPDTest, UPDPackageTest)
{
    socket_addr_t test_addr("127.0.0.1", 2224);
    event_context_t ctx(event_strategy::epoll);
    udp::server_t server;

    server.bind(ctx, test_addr);
    server.run([&server, &ctx]() {
        auto socket = server.get_socket();
        socket_buffer_t buffer(test_data.size());
        buffer.expect().origin_length();
        socket_addr_t addr;
        GTEST_ASSERT_EQ(co::await(socket_aread_from, socket, buffer, addr), io_result::ok);
        GTEST_ASSERT_EQ(buffer.to_string(), test_data);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(socket_awrite_to, socket, buffer, addr), io_result::ok);
    });

    udp::client_t client;
    client.connect(ctx, test_addr, false);
    client.run([&client, &test_addr, &ctx]() {
        auto socket = client.get_socket();
        socket_buffer_t buffer(test_data);
        buffer.expect().origin_length();
        socket_addr_t addr = test_addr;
        GTEST_ASSERT_EQ(co::await(socket_awrite_to, socket, buffer, addr), io_result::ok);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(socket_aread_from, socket, buffer, addr), io_result::ok);
        GTEST_ASSERT_EQ(buffer.to_string(), test_data);
        ctx.exit_all(0);
    });
    ctx.run();
}