#pragma once
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <queue>
#include <vector>

namespace net
{
using microsecond_t = int64_t;
using callback_t = std::function<void()>;
// 10ms
inline constexpr microsecond_t timer_min_precision = 10000;
struct timer_t
{
    microsecond_t timepoint;
    std::function<void()> callback;
    timer_t(microsecond_t timepoint, std::function<void()> callback)
        : timepoint(timepoint)
        , callback(callback)
    {
    }
};

struct timer_cmp
{
    bool operator()(timer_t const &lh, timer_t const &rh) const { return lh.timepoint < rh.timepoint; }
};

struct time_manager_t
{
    microsecond_t precision;
    std::priority_queue<timer_t, std::vector<timer_t>, timer_cmp> queue;

    time_manager_t() {}

    void tick();
    void insert(timer_t timer);
    microsecond_t next_tick_timepoint();
};

std::unique_ptr<time_manager_t> create_time_manager(microsecond_t precision = timer_min_precision);

microsecond_t get_current_time();

} // namespace net