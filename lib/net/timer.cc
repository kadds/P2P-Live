#include "net/timer.hpp"
#include <chrono>
#include <sys/time.h>

namespace net
{
std::unique_ptr<time_manager_t> create_time_manager(microsecond_t precision)
{
    std::unique_ptr<time_manager_t> time_wheel = std::make_unique<time_manager_t>();
    time_wheel->precision = precision;

    return std::move(time_wheel);
}

void time_manager_t::tick()
{
    auto us = get_current_time();
    while (!queue.empty())
    {
        auto timer = queue.top();
        if (timer.timepoint > us)
        {
            break;
        }
        queue.pop();
        timer.callback();
    }
}

void time_manager_t::insert(timer_t timer)
{
    timer.timepoint = (timer.timepoint + precision - 1) / precision * precision;
    if (timer.timepoint <= get_current_time())
    {
        timer.callback();
        return;
    }
    queue.push(timer);
}

microsecond_t time_manager_t::next_tick_timepoint()
{
    if (!queue.empty())
    {
        auto timer = queue.top();
        return timer.timepoint;
    }

    return 0xFFFFFFFFFFFFFFFFLLU;
}

microsecond_t get_current_time()
{
    std::chrono::high_resolution_clock clock;
    clock.now();
    struct timeval timeval;
    gettimeofday(&timeval, nullptr);
    return timeval.tv_usec + (microsecond_t)timeval.tv_sec * 1000000;
}

} // namespace net