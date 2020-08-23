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
    event_context_t ctx(event_strategy::AUTO);
    microsecond_t point = get_current_time();
    microsecond_t point2;
    // 500ms
    microsecond_t span = 500000;

    event_loop_t::current().add_timer(::net::make_timer(span, [&ctx, &point2]() {
        point2 = get_current_time();
        ctx.exit_all(1);
    }));
    ctx.run();
    GTEST_ASSERT_GE(point2 - point, span);
}

TEST(TimerTest, TimerLongTick)
{
    event_context_t ctx(event_strategy::AUTO, 500000);
    // 500ms
    microsecond_t point = get_current_time();
    microsecond_t point2;
    // 550ms
    microsecond_t span = 550000;

    event_loop_t::current().add_timer(::net::make_timer(span, [&ctx, &point2]() {
        point2 = get_current_time();
        ctx.exit_all(1);
    }));
    ctx.run();
    GTEST_ASSERT_GE(point2 - point, span);
}

void work()
{
    event_loop_t::current().add_timer(::net::timer_t(get_current_time() + 200000, []() { work(); }));
    // work load
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
}

TEST(TimerTest, TimerFullWorkLoad)
{
    // 100ms
    event_context_t ctx(event_strategy::AUTO, 100000);

    microsecond_t point = get_current_time();
    microsecond_t point2;
    // 800ms
    microsecond_t span = 800000;
    event_loop_t::current().add_timer(::net::make_timer(span, [&ctx, &point2]() {
        point2 = get_current_time();
        ctx.exit_all(1);
    }));

    event_loop_t::current().add_timer(::net::timer_t(get_current_time() + 200000, []() { work(); }));

    ctx.run();
    GTEST_ASSERT_GE(point2 - point, span);
}

TEST(TimerTest, TimerRemove)
{
    event_context_t ctx(event_strategy::AUTO, 100000);
    microsecond_t point = get_current_time();
    microsecond_t point2;

    auto tick = event_loop_t::current().add_timer(make_timer(make_timespan(1, 500), []() {
        std::string str = "timer remove failed";
        GTEST_ASSERT_EQ(str, "");
    }));

    event_loop_t::current().add_timer(
        make_timer(make_timespan(1), [&tick]() { event_loop_t::current().remove_timer(tick); }));
    event_loop_t::current().add_timer(make_timer(make_timespan(1, 800), [&ctx, &tick]() { ctx.exit_all(0); }));

    ctx.run();
}

TEST(TimerTest, SocketTimer)
{
    socket_addr_t test_addr("127.0.0.1", 2222);
    event_context_t ctx(event_strategy::AUTO);
    tcp::server_t server;

    // 500ms
    microsecond_t span = 500000, point = 0;

    server.on_client_join([span, &point](tcp::server_t &s, tcp::connection_t conn) {
        point = get_current_time();
        conn.get_socket()->sleep(span);
        socket_buffer_t buffer = socket_buffer_t::from_string("hi");
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(tcp::conn_awrite, conn, buffer), io_result::ok);
    });

    server.listen(ctx, test_addr, 1, true);

    tcp::client_t client;
    client
        .on_server_connect([](tcp::client_t &c, tcp::connection_t conn) {
            socket_buffer_t buffer(2);
            buffer.expect().origin_length();
            GTEST_ASSERT_EQ(co::await(tcp::conn_aread, conn, buffer), io_result::ok);
        })
        .on_server_disconnect([&ctx](tcp::client_t &c, tcp::connection_t conn) { ctx.exit_all(0); });

    client.connect(ctx, test_addr, 1000);

    ctx.run();
    GTEST_ASSERT_GE(get_current_time() - point, span);
}
