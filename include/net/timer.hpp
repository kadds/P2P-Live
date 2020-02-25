#pragma once
#include "net.hpp"
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <queue>
#include <vector>

namespace net
{
using microsecond_t = u64;
using timer_callback_t = std::function<void()>;
// 1ms
inline constexpr microsecond_t timer_min_precision = 1000;
using timer_id = int64_t;

struct timer_t
{
    microsecond_t timepoint;
    timer_callback_t callback;
    timer_t(microsecond_t timepoint, std::function<void()> callback)
        : timepoint(timepoint)
        , callback(callback)
    {
    }
};

struct timer_slot_t
{
    microsecond_t timepoint;
    std::vector<std::pair<timer_callback_t, bool>> callbacks;
    timer_slot_t(microsecond_t tp)
        : timepoint(tp)
    {
    }
};

using map_t = std::unordered_map<microsecond_t, timer_slot_t *>;

timer_t make_timer(microsecond_t span, timer_callback_t callback);

struct timer_cmp
{
    bool operator()(timer_slot_t *lh, timer_slot_t *rh) const { return lh->timepoint > rh->timepoint; }
};

struct timer_registered_t
{
    timer_id id;
    microsecond_t timepoint;
};

/// timer is run in one thread, and is not thread-safety. don't add timer from other thread
struct time_manager_t
{
    microsecond_t precision;
    std::priority_queue<timer_slot_t *, std::vector<timer_slot_t *>, timer_cmp> queue;
    map_t map;

    time_manager_t() {}

    void tick();
    timer_registered_t insert(timer_t timer);
    void cancel(timer_registered_t reg);
    microsecond_t next_tick_timepoint();
};

std::unique_ptr<time_manager_t> create_time_manager(microsecond_t precision = timer_min_precision);

microsecond_t get_current_time();
microsecond_t get_timestamp();

constexpr microsecond_t make_timespan(int second, int ms = 0, int us = 0)
{
    return (u64)second * 1000000 + (u64)ms * 1000 + us;
}

constexpr microsecond_t make_timespan_full() { return 0xFFFFFFFFFFFFFFFFULL; }

} // namespace net