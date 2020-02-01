#include "net/udp.hpp"
#include "net/co.hpp"
#include "net/event.hpp"
#include "net/socket.hpp"
#include "net/socket_buffer.hpp"
#include <functional>
#include <gtest/gtest.h>

TEST(UPDPackageTest, UPDTestInput)
{
    using namespace net;
    std::string test_data = "test string";
    socket_addr_t test_addr("127.0.0.1", 2224);
    init_lib();
    udp::server_t server;
    event_context_t ctx(event_strategy::epoll);
    event_loop_t loop;
    ctx.add_event_loop(&loop);

    server.bind(ctx, test_addr);
    server.get_socket()->startup_coroutine(co::coroutine_t::create([&server, &test_data, &loop]() {
        auto socket = server.get_socket();
        socket_buffer_t buffer(test_data.size());
        buffer.expect().origin_length();
        socket_addr_t addr;
        co::await(socket_aread_from, socket, buffer, addr);
        GTEST_ASSERT_EQ((const char *)buffer.get(), test_data);
        buffer.expect().origin_length();
        co::await(socket_awrite_to, socket, buffer, addr);
        server.close();
        loop.exit(0);
    }));

    udp::client_t client;
    client.connect(ctx, test_addr, true);
    client.get_socket()->startup_coroutine(co::coroutine_t::create([&client, &test_data, &test_addr]() {
        auto socket = client.get_socket();
        socket_buffer_t buffer(test_data);
        buffer.expect().origin_length();
        socket_addr_t addr = test_addr;
        co::await(socket_awrite_to, socket, buffer, addr);
        buffer.expect().origin_length();
        co::await(socket_aread_from, socket, buffer, addr);
        GTEST_ASSERT_EQ((const char *)buffer.get(), test_data);
        client.close();
    }));
    loop.run();
}

TEST(UPDConnectionTest, UPDTestInput) {}
