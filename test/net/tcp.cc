#include "net/tcp.hpp"
#include "net/co.hpp"
#include "net/event.hpp"
#include "net/socket.hpp"
#include "net/socket_buffer.hpp"
#include <functional>
#include <gtest/gtest.h>
#include <memory>

using namespace net;
static std::string test_data = "test string";
TEST(TCPTest, StreamConnection)
{
    socket_addr_t test_addr("127.0.0.1", 2222);
    event_context_t ctx(event_strategy::epoll);
    event_loop_t loop;
    ctx.add_event_loop(&loop);
    tcp::server_t server;

    server.at_client_join([](tcp::server_t &s, tcp::connection_t conn) {
        socket_buffer_t buffer(test_data.size());
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(tcp::connection_aread, conn, buffer), io_result::ok);

        GTEST_ASSERT_EQ(buffer.to_string(), test_data);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(tcp::connection_awrite, conn, buffer), io_result::ok);
    });
    server.listen(ctx, test_addr, 1, true);

    tcp::client_t client;
    client
        .at_server_connect([](tcp::client_t &c, tcp::connection_t conn) {
            socket_buffer_t buffer(test_data);
            buffer.expect().origin_length();
            GTEST_ASSERT_EQ(co::await(tcp::connection_awrite, conn, buffer), io_result::ok);
            buffer.expect().origin_length();
            GTEST_ASSERT_EQ(co::await(tcp::connection_aread, conn, buffer), io_result::ok);
            GTEST_ASSERT_EQ(buffer.to_string(), test_data);
        })
        .at_server_disconnect([&loop](tcp::client_t &c, tcp::connection_t conn) { loop.exit(0); });

    client.connect(ctx, test_addr, net::make_timespan_full());
    loop.add_timer(make_timer(make_timespan(1, 500, 0), [&loop]() {
        loop.exit(-1);
        std::string str = "timeout";
        GTEST_ASSERT_EQ(str, "");
    }));
    loop.run();
}

constexpr u64 test_size = 20480;
constexpr u64 test_bit = 512;
struct test_package_t
{
    u8 data[test_size];
};

TEST(TCPTest, PacketConnection)
{
    socket_addr_t test_addr("127.0.0.1", 2222);
    event_context_t ctx(event_strategy::epoll);
    event_loop_t loop;
    ctx.add_event_loop(&loop);
    tcp::server_t server;

    server.at_client_join([](tcp::server_t &s, tcp::connection_t conn) {
        set_socket_send_buffer_size(conn.get_socket(), 2000);
        /// int r = get_socket_send_buffer_size(conn.get_socket());

        std::unique_ptr<test_package_t> package = std::make_unique<test_package_t>();

        tcp::package_head_t head;
        head.version = 4;
        socket_buffer_t buffer((byte *)package.get(), sizeof(test_package_t));
        head.v4.msg_type = 5;
        package->data[test_bit] = 5;

        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(tcp::connection_awrite_package, conn, head, buffer), io_result::ok);

        head.version = 4;
        head.v4.msg_type = 4;
        package->data[test_bit] = 4;
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(tcp::connection_awrite_package, conn, head, buffer), io_result::ok);

        head.version = 4;
        head.v4.msg_type = 3;
        package->data[test_bit] = 3;
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(tcp::connection_awrite_package, conn, head, buffer), io_result::ok);

        head.version = 4;
        head.v4.msg_type = 2;
        package->data[test_bit] = 2;
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(tcp::connection_awrite_package, conn, head, buffer), io_result::ok);
    });
    server.listen(ctx, test_addr, 1, true);

    tcp::client_t client;
    client
        .at_server_connect([](tcp::client_t &c, tcp::connection_t conn) {
            set_socket_recv_buffer_size(conn.get_socket(), 1000);
            /// int r = get_socket_recv_buffer_size(conn.get_socket());

            std::unique_ptr<test_package_t> package = std::make_unique<test_package_t>();
            socket_buffer_t buffer((byte *)package.get(), sizeof(test_package_t));
            for (int i = 0; i < 4; i++)
            {
                tcp::package_head_t head;
                GTEST_ASSERT_EQ(co::await(tcp::connection_aread_package_head, conn, head), io_result::ok);
                GTEST_ASSERT_EQ(head.version, 4);
                GTEST_ASSERT_EQ(head.v4.size, test_size);
                buffer.expect().origin_length();
                GTEST_ASSERT_EQ(co::await(tcp::connection_aread_package_content, conn, buffer), io_result::ok);

                GTEST_ASSERT_EQ(head.v4.msg_type, package->data[test_bit]);
            }
        })
        .at_server_disconnect([&loop](tcp::client_t &c, tcp::connection_t conn) { loop.exit(0); });

    client.connect(ctx, test_addr, net::make_timespan_full());
    loop.add_timer(make_timer(make_timespan(5), [&loop]() {
        loop.exit(-1);
        std::string str = "timeout";
        GTEST_ASSERT_EQ(str, "");
    }));
    loop.run();
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