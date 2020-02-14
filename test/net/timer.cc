#include "net/timer.hpp"
#include "net/co.hpp"
#include "net/event.hpp"
#include "net/socket.hpp"
#include "net/socket_buffer.hpp"
#include "net/tcp.hpp"
#include <functional>
#include <gtest/gtest.h>

using namespace net;

TEST(TimerTest, TimerShortTick)
{
    event_context_t ctx(event_strategy::select);
    event_loop_t loop;
    ctx.add_event_loop(&loop);
    microsecond_t point = get_current_time();
    microsecond_t point2;
    // 500ms
    microsecond_t span = 500000;

    loop.add_timer(::net::make_timer(span, [&loop, &point2]() {
        point2 = get_current_time();
        loop.exit(1);
    }));
    loop.run();
    GTEST_ASSERT_GE(point2 - point, span);
    GTEST_ASSERT_LT(point2 - point, span + timer_min_precision * 2);
}

TEST(TimerTest, TimerLongTick)
{
    event_context_t ctx(event_strategy::epoll);
    // 500ms
    auto time_wheel = create_time_manager(500000);
    event_loop_t loop(std::move(time_wheel));
    ctx.add_event_loop(&loop);
    microsecond_t point = get_current_time();
    microsecond_t point2;
    // 550ms
    microsecond_t span = 550000;

    loop.add_timer(::net::make_timer(span, [&loop, &point2]() {
        point2 = get_current_time();
        loop.exit(1);
    }));
    loop.run();
    GTEST_ASSERT_GE(point2 - point, span);
    GTEST_ASSERT_LT(point2 - point, span + 500000 * 2);
}

void work(event_loop_t &loop)
{
    loop.add_timer(::net::timer_t(get_current_time() + 200000, [&loop]() { work(loop); }));
    // work load
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
}

TEST(TimerTest, TimerFullWorkLoad)
{
    event_context_t ctx(event_strategy::epoll);
    // 100ms
    auto time_wheel = create_time_manager(100000);
    event_loop_t loop(std::move(time_wheel));
    ctx.add_event_loop(&loop);
    microsecond_t point = get_current_time();
    microsecond_t point2;
    // 800ms
    microsecond_t span = 800000;
    loop.add_timer(::net::make_timer(span, [&loop, &point2]() {
        point2 = get_current_time();
        loop.exit(1);
    }));

    loop.add_timer(::net::timer_t(get_current_time() + 200000, [&loop]() { work(loop); }));

    loop.run();
    GTEST_ASSERT_GE(point2 - point, span);
}

TEST(TimerTest, SocketTimer)
{
    socket_addr_t test_addr("127.0.0.1", 2222);
    event_context_t ctx(event_strategy::epoll);
    event_loop_t loop;
    ctx.add_event_loop(&loop);
    tcp::server_t server;

    // 500ms
    microsecond_t span = 500000, point = 0;

    server.at_client_join([span, &point](tcp::server_t &s, socket_t *socket) {
        point = get_current_time();
        socket->sleep(span);
        socket_buffer_t buffer("hi");
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(socket_awrite, socket, buffer), io_result::ok);
    });

    server.listen(ctx, test_addr, 1, true);

    tcp::client_t client;
    client
        .at_server_connect([](tcp::client_t &c, socket_t *socket) {
            socket_buffer_t buffer(2);
            buffer.expect().origin_length();
            GTEST_ASSERT_EQ(co::await(socket_aread, socket, buffer), io_result::ok);
        })
        .at_server_disconnect([&loop](tcp::client_t &c, socket_t *socket) { loop.exit(0); });

    client.connect(ctx, test_addr, 1000);

    loop.run();
    server.close_server();
    GTEST_ASSERT_GE(get_current_time() - point, span);
}
