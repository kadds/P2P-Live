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
using callback_t = std::function<void()>;
// 10ms
inline constexpr microsecond_t timer_min_precision = 10000;
using timer_id = int64_t;

struct timer_t
{
    microsecond_t timepoint;
    callback_t callback;
    timer_t(microsecond_t timepoint, std::function<void()> callback)
        : timepoint(timepoint)
        , callback(callback)
    {
    }
};

struct timer_slot_t
{
    microsecond_t timepoint;
    std::vector<std::pair<callback_t, bool>> callbacks;
    timer_slot_t(microsecond_t tp)
        : timepoint(tp)
    {
    }
};

using map_t = std::unordered_map<microsecond_t, timer_slot_t *>;

timer_t make_timer(microsecond_t span, callback_t callback);

struct timer_cmp
{
    bool operator()(timer_slot_t *lh, timer_slot_t *rh) const { return lh->timepoint < rh->timepoint; }
};

struct time_manager_t
{
    microsecond_t precision;
    std::priority_queue<timer_slot_t *, std::vector<timer_slot_t *>, timer_cmp> queue;
    map_t map;

    time_manager_t() {}

    void tick();
    timer_id insert(timer_t timer);
    void cancel(microsecond_t timepoint, timer_id id);
    microsecond_t next_tick_timepoint();
};

std::unique_ptr<time_manager_t> create_time_manager(microsecond_t precision = timer_min_precision);

microsecond_t get_current_time();
microsecond_t get_timestamp();

} // namespace net