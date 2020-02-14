#include "net/timer.hpp"
#include <chrono>
#include <sys/time.h>

namespace net
{
timer_t make_timer(microsecond_t span, callback_t callback) { return timer_t(span + get_current_time(), callback); }

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
        auto timers = queue.top();
        if (timers->timepoint > us)
        {
            break;
        }
        queue.pop();
        for (auto &i : timers->callbacks)
        {
            if (i.second)
            {
                i.first();
            }
        }
        map.erase(timers->timepoint);
        delete timers;
    }
}

timer_id time_manager_t::insert(timer_t timer)
{
    timer.timepoint = (timer.timepoint + precision - 1) / precision * precision;

    auto it = map.find(timer.timepoint);
    if (it == map.end())
    {
        it = map.emplace(timer.timepoint, new timer_slot_t(timer.timepoint)).first;
        queue.push(it->second);
    }

    it->second->callbacks.emplace_back(timer.callback, true);
    return it->second->callbacks.size();
}

void time_manager_t::cancel(microsecond_t timepoint, timer_id id)
{
    auto it = map.find(timepoint);
    if (it != map.end())
    {
        it->second->callbacks[id - 1].second = false;
    }
}

microsecond_t time_manager_t::next_tick_timepoint()
{
    if (!queue.empty())
    {
        auto timer = queue.top();
        return timer->timepoint;
    }

    return 0xFFFFFFFFFFFFFFFFLLU;
}

microsecond_t get_current_time()
{
    /// FXIME: Use std clock
    // std::chrono::high_resolution_clock clock;
    // clock.now();
    struct timeval timeval;
    gettimeofday(&timeval, nullptr);
    return timeval.tv_usec + (microsecond_t)timeval.tv_sec * 1000000;
}

microsecond_t get_timestamp()
{
    return std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now())
        .time_since_epoch()
        .count();
}

} // namespace net