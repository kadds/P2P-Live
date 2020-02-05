#include "net/timer.hpp"
#include "net/co.hpp"
#include "net/event.hpp"
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

    loop.add_timer(::net::timer_t(get_current_time() + span, [&loop, &point2]() {
        point2 = get_current_time();
        loop.exit(1);
    }));
    loop.run();
    GTEST_ASSERT_GE(point2 - point, span);
    GTEST_ASSERT_LT(point2 - point, span + timer_min_precision);
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

    loop.add_timer(::net::timer_t(get_current_time() + span, [&loop, &point2]() {
        point2 = get_current_time();
        loop.exit(1);
    }));
    loop.run();
    GTEST_ASSERT_GE(point2 - point, span);
    GTEST_ASSERT_LT(point2 - point, span + 500000);
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
    loop.add_timer(::net::timer_t(get_current_time() + span, [&loop, &point2]() {
        point2 = get_current_time();
        loop.exit(1);
    }));
    loop.add_timer(::net::timer_t(get_current_time() + 200000, [&loop]() { work(loop); }));

    loop.run();
    GTEST_ASSERT_GE(point2 - point, span);
}
