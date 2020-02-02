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
    event_loop_t loop;
    ctx.add_event_loop(&loop);
    udp::server_t server;

    server.bind(ctx, test_addr);
    server.get_socket()->startup_coroutine(co::coroutine_t::create([&server, &loop]() {
        auto socket = server.get_socket();
        socket_buffer_t buffer(test_data.size());
        buffer.expect().origin_length();
        socket_addr_t addr;
        GTEST_ASSERT_EQ(co::await(socket_aread_from, socket, buffer, addr), io_result::ok);
        GTEST_ASSERT_EQ(buffer.to_string(), test_data);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(socket_awrite_to, socket, buffer, addr), io_result::ok);
        loop.exit(0);
        server.close();
    }));

    udp::client_t client;
    client.connect(ctx, test_addr, false);
    client.get_socket()->startup_coroutine(co::coroutine_t::create([&client, &test_addr]() {
        auto socket = client.get_socket();
        socket_buffer_t buffer(test_data);
        buffer.expect().origin_length();
        socket_addr_t addr = test_addr;
        GTEST_ASSERT_EQ(co::await(socket_awrite_to, socket, buffer, addr), io_result::ok);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(socket_aread_from, socket, buffer, addr), io_result::ok);
        GTEST_ASSERT_EQ(buffer.to_string(), test_data);
        client.close();
    }));
    loop.run();
}

void send_client(event_context_t &ctx, socket_addr_t addr)
{
    udp::client_t client;
    client.connect(ctx, addr, false);
    client.get_socket()->startup_coroutine(co::coroutine_t::create([&client, addr]() {
        auto socket = client.get_socket();
        socket_buffer_t buffer(test_data);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(socket_awrite_to, socket, buffer, addr), io_result::ok);
        buffer.expect().origin_length();
        socket_addr_t waddr = addr;
        GTEST_ASSERT_EQ(co::await(socket_aread_from, socket, buffer, waddr), io_result::ok);
        GTEST_ASSERT_EQ(buffer.to_string(), test_data);
        GTEST_ASSERT_EQ(addr, waddr);
        client.close();
    }));
}

TEST(UPDTest, UPDConnectionTest)
{
    socket_addr_t test_addr("127.0.0.1", 2224);
    event_context_t ctx(event_strategy::epoll);
    event_loop_t loop;
    ctx.add_event_loop(&loop);

    int counter = 0;

    udp::connectable_server_t server;
    server.bind(ctx, test_addr);
    server.listen_message_recv(
        [&loop, &counter](udp::connectable_server_t &server, socket_buffer_t buffer, socket_addr_t addr) {
            auto socket = server.get_socket();
            GTEST_ASSERT_EQ(buffer.to_string(), test_data);
            GTEST_ASSERT_EQ(co::await(socket_awrite_to, socket, buffer, addr), io_result::ok);
            if (++counter > 2)
            {
                loop.exit(0);
                server.close();
            }
        });
    send_client(ctx, test_addr);
    send_client(ctx, test_addr);
    send_client(ctx, test_addr);

    loop.run();
    GTEST_ASSERT_EQ(counter, 3);
}
