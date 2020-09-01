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
    event_context_t ctx(event_strategy::AUTO);
    tcp::server_t server;

    server.on_client_join([](tcp::server_t &s, tcp::connection_t conn) {
        socket_buffer_t buffer = socket_buffer_t::from_string(test_data);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(tcp::conn_awrite, conn, buffer), io_result::ok);

        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(tcp::conn_aread, conn, buffer), io_result::ok);
        GTEST_ASSERT_EQ(buffer.to_string(), test_data);
    });
    server.listen(ctx, test_addr, 1, true);

    tcp::client_t client;
    client
        .on_server_connect([](tcp::client_t &c, tcp::connection_t conn) {
            socket_buffer_t buffer(test_data.size());
            buffer.expect().origin_length();
            GTEST_ASSERT_EQ(co::await(tcp::conn_aread, conn, buffer), io_result::ok);
            GTEST_ASSERT_EQ(buffer.to_string(), test_data);
            buffer.expect().origin_length();
            GTEST_ASSERT_EQ(co::await(tcp::conn_awrite, conn, buffer), io_result::ok);
        })
        .on_server_disconnect([&ctx](tcp::client_t &c, tcp::connection_t conn) { ctx.exit_all(0); });

    client.connect(ctx, test_addr, net::make_timespan_full());
    event_loop_t::current().add_timer(make_timer(make_timespan(1, 500, 0), [&ctx]() {
        ctx.exit_all(-1);
        std::string str = "timeout";
        GTEST_ASSERT_EQ(str, "");
    }));
    ctx.run();
}

TEST(TCPTest, LargeStreamConnection)
{
    socket_addr_t test_addr("127.0.0.1", 2203);
    event_context_t ctx(event_strategy::AUTO);
    tcp::server_t server;

    server.on_client_join([](tcp::server_t &s, tcp::connection_t conn) {
        socket_buffer_t buffer(test_data.size());
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(tcp::conn_aread, conn, buffer), io_result::ok);

        GTEST_ASSERT_EQ(buffer.to_string(), test_data);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(tcp::conn_awrite, conn, buffer), io_result::ok);
    });
    server.listen(ctx, test_addr, 100, true);

    const int cnt = 5;
    std::vector<tcp::client_t> clients;
    clients.resize(cnt);
    int ok = 0;

    for (int i = 0; i < cnt; i++)
    {
        auto &client = clients[i];
        client
            .on_server_connect([&ok, &cnt, &ctx](tcp::client_t &c, tcp::connection_t conn) {
                socket_buffer_t buffer = socket_buffer_t::from_string(test_data);
                buffer.expect().origin_length();
                GTEST_ASSERT_EQ(co::await(tcp::conn_awrite, conn, buffer), io_result::ok);
                buffer.expect().origin_length();
                GTEST_ASSERT_EQ(co::await(tcp::conn_aread, conn, buffer), io_result::ok);
                GTEST_ASSERT_EQ(buffer.to_string(), test_data);
                ok++;
                if (ok >= cnt)
                {
                    ctx.exit_all(0);
                }
            })
            .on_server_disconnect([&ctx, &ok, &cnt](tcp::client_t &c, tcp::connection_t conn) {
                if (ok >= cnt)
                {
                    ctx.exit_all(0);
                }
                else
                {
                }
            });

        client.connect(ctx, test_addr, net::make_timespan(5));
    }
    event_loop_t::current().add_timer(make_timer(make_timespan(15), [&ctx]() {
        ctx.exit_all(-1);
        std::string str = "timeout";
        GTEST_ASSERT_EQ(str, "");
    }));
    ctx.run();
    GTEST_ASSERT_EQ(ok, cnt);
}

constexpr u64 test_size = 20000;
constexpr u64 x_size = 640;
struct test_package_t
{
    u8 data[test_size];
};

TEST(TCPTest, PacketConnection)
{
    socket_addr_t test_addr("127.0.0.1", 2222);
    event_context_t ctx(event_strategy::AUTO);
    tcp::server_t server;

    server.on_client_join([](tcp::server_t &s, tcp::connection_t conn) {
        set_socket_send_buffer_size(conn.get_socket(), x_size);
        /// int r = get_socket_send_buffer_size(conn.get_socket());

        std::unique_ptr<test_package_t> package = std::make_unique<test_package_t>();
        memset(package.get(), 0xFFFF, sizeof(test_package_t));

        tcp::package_head_t head;
        socket_buffer_t buffer((byte *)package.get(), sizeof(test_package_t));

        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(tcp::conn_awrite_packet, conn, head, buffer), io_result::ok);

        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(tcp::conn_awrite_packet, conn, head, buffer), io_result::ok);

        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(tcp::conn_awrite_packet, conn, head, buffer), io_result::ok);

        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(tcp::conn_awrite_packet, conn, head, buffer), io_result::ok);
        // s.get_socket()->sleep(100000);
    });
    server.listen(ctx, test_addr, 1, true);

    tcp::client_t client;
    client
        .on_server_connect([](tcp::client_t &c, tcp::connection_t conn) {
            set_socket_recv_buffer_size(conn.get_socket(), x_size);
            /// int r = get_socket_recv_buffer_size(conn.get_socket());

            std::unique_ptr<test_package_t> package = std::make_unique<test_package_t>();
            socket_buffer_t buffer((byte *)package.get(), sizeof(test_package_t));
            std::unique_ptr<test_package_t> target_package = std::make_unique<test_package_t>();
            memset(target_package.get(), 0xFFFF, sizeof(test_package_t));
            for (int i = 0; i < 4; i++)
            {
                tcp::package_head_t head;
                GTEST_ASSERT_EQ(co::await(tcp::conn_aread_packet_head, conn, head), io_result::ok);
                GTEST_ASSERT_EQ(head.size, test_size);
                buffer.expect().origin_length();
                GTEST_ASSERT_EQ(co::await(tcp::conn_aread_packet_content, conn, buffer), io_result::ok);
                GTEST_ASSERT_EQ(memcmp(package.get(), target_package.get(), sizeof(test_package_t)), 0);
                memset(package.get(), 0, sizeof(test_package_t));
            }
        })
        .on_server_disconnect([&ctx](tcp::client_t &c, tcp::connection_t conn) { ctx.exit_all(0); });

    client.connect(ctx, test_addr, net::make_timespan_full());
    event_loop_t::current().add_timer(make_timer(make_timespan(5), [&ctx]() {
        ctx.exit_all(0);
        std::string str = "timeout";
        GTEST_ASSERT_EQ(str, "");
    }));
    ctx.run();
}

TEST(TCPTest, TCPTimeout)
{
    socket_addr_t test_addr("8.8.8.8", 2222);
    event_context_t ctx(event_strategy::AUTO);

    tcp::client_t client;
    client.on_server_error([&ctx](tcp::client_t &c, socket_t *socket, socket_addr_t addr, connection_state state) {
        GTEST_ASSERT_EQ((int)state, (int)connection_state::timeout);
        ctx.exit_all(0);
    });

    client.connect(ctx, test_addr, net::make_timespan(1));
    event_loop_t::current().add_timer(make_timer(make_timespan(1, 500, 0), [&ctx]() {
        ctx.exit_all(-1);
        std::string str = "timeout test failed";
        GTEST_ASSERT_EQ(str, "");
    }));
    ctx.run();
}

static void thread_main(event_context_t *context)
{
    // context->run();
}

static void client_main(tcp::client_t &client, event_context_t *context, net::socket_addr_t test_addr,
                        std::atomic_int &ref)
{
    client
        .on_server_connect([](tcp::client_t &c, tcp::connection_t conn) {
            socket_buffer_t buffer = socket_buffer_t::from_string(test_data);
            buffer.expect().origin_length();
            GTEST_ASSERT_EQ(co::await(tcp::conn_awrite, conn, buffer), io_result::ok);
            buffer.expect().origin_length();
            GTEST_ASSERT_EQ(co::await(tcp::conn_aread, conn, buffer), io_result::ok);
            GTEST_ASSERT_EQ(buffer.to_string(), test_data);
        })
        .on_server_disconnect([context, &ref](tcp::client_t &c, tcp::connection_t conn) {
            if (--ref <= 0)
                context->exit_all(0);
        })
        .on_server_error([context](tcp::client_t &c, socket_t *s, socket_addr_t addr, connection_state state) {
            GTEST_ASSERT_EQ(std::string(""), std::string("connect timeout"));
        });

    client.connect(*context, test_addr, net::make_timespan(1));
}

TEST(TCPTest, MultiThreadTest)
{
    socket_addr_t test_addr("127.0.0.1", 2129);
    event_context_t ctx(event_strategy::AUTO);
    tcp::server_t server;

    server.on_client_join([](tcp::server_t &s, tcp::connection_t conn) {
        socket_buffer_t buffer(test_data.size());
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(tcp::conn_aread, conn, buffer), io_result::ok);

        GTEST_ASSERT_EQ(buffer.to_string(), test_data);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(tcp::conn_awrite, conn, buffer), io_result::ok);
    });
    constexpr int counts = 1;
    server.listen(ctx, test_addr, counts, true);
    constexpr int threadsc = 4;

    tcp::client_t clients[counts];
    std::atomic_int ref = counts;

    std::unique_ptr<std::thread> threads[threadsc];

    for (auto i = 0; i < threadsc; i++)
    {
        threads[i] = std::make_unique<std::thread>(std::bind(&thread_main, &ctx));
    }
    for (auto &c : clients)
    {
        client_main(c, &ctx, test_addr, ref);
    }

    event_loop_t::current().add_timer(make_timer(make_timespan(5000), [&ctx]() {
        ctx.exit_all(-1);
        std::string str = "timeout";
        GTEST_ASSERT_EQ(str, "");
    }));
    ctx.run();

    for (auto i = 0; i < threadsc; i++)
    {
        threads[i]->join();
    }
}