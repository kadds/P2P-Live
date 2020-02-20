#include "net/event.hpp"
#include "net/co.hpp"
#include "net/epoll.hpp"
#include "net/execute_context.hpp"
#include "net/select.hpp"
#include "net/socket.hpp"
#include <algorithm>
#include <iostream>
#include <signal.h>

namespace net
{
event_context_t::event_context_t(event_strategy strategy)
    : strategy(strategy)
{
}

thread_local event_loop_t *thread_in_loop;

event_loop_t::event_loop_t()
    : is_exit(false)
    , exit_code(0)
{
    time_manager = create_time_manager();
    thread_in_loop = this;
}

event_loop_t::event_loop_t(std::unique_ptr<time_manager_t> time_manager)
    : is_exit(false)
    , exit_code(0)
    , time_manager(std::move(time_manager))
{
    thread_in_loop = this;
}

event_loop_t::~event_loop_t() { thread_in_loop = nullptr; }

void event_loop_t::add_event_handler(handle_t handle, event_handler_t *handler) { event_map[handle] = handler; }

void event_loop_t::remove_event_handler(handle_t handle, event_handler_t *handler)
{
    unlink(handle, event_type::error | event_type::writable | event_type::readable);

    auto it = event_map.find(handle);
    if (it != event_map.end())
        event_map.erase(it);
}

event_loop_t &event_loop_t::link(handle_t handle, event_type_t type)
{
    demuxer->add(handle, type);
    return *this;
}

event_loop_t &event_loop_t::unlink(handle_t handle, event_type_t type)
{
    demuxer->remove(handle, type);
    return *this;
}

int event_loop_t::run()
{
    event_type_t type;
    while (!is_exit)
    {
        microsecond_t cur_time = get_current_time();
        auto target_time = time_manager->next_tick_timepoint();
        if (cur_time >= target_time)
        {
            time_manager->tick();
        }
        dispatcher.dispatch();
        auto next = time_manager->next_tick_timepoint();
        cur_time = get_current_time();
        microsecond_t timeout;
        if (next > cur_time)
            timeout = next - cur_time;
        else
            timeout = 0;

        if (is_exit)
            return exit_code;

        auto handle = demuxer->select(&type, &timeout);
        if (handle != 0)
        {
            auto ev_it = event_map.find(handle);
            if (ev_it != event_map.end())
            {
                auto handler = ev_it->second;
                handler->on_event(*context, type);
            }
        }
        dispatcher.dispatch();
    }
    return exit_code;
}

void event_loop_t::exit(int code)
{
    exit_code = code;
    is_exit = true;
}

int event_loop_t::load_factor() { return event_map.size(); }

event_loop_t &event_loop_t::current() { return *thread_in_loop; }

void event_context_t::add_executor(execute_context_t *exectx) { exectx->loop = &select_loop(); }

void event_context_t::add_executor(execute_context_t *exectx, event_loop_t *loop) { exectx->loop = loop; }

void event_context_t::remove_executor(execute_context_t *exectx) { exectx->loop = nullptr; }

timer_registered_t event_loop_t::add_timer(timer_t timer) { return time_manager->insert(timer); }

void event_loop_t::remove_timer(timer_registered_t reg) { time_manager->cancel(reg); }

execute_thread_dispatcher_t &event_loop_t::get_dispatcher() { return dispatcher; }

event_loop_t &event_context_t::select_loop()
{
    event_loop_t *min_load_loop;
    {
        std::shared_lock<std::shared_mutex> lock(loop_mutex);
        min_load_loop = loops[0];
        int min_fac = min_load_loop->load_factor();
        for (auto loop : loops)
        {
            int fac = loop->load_factor();
            if (fac < min_fac)
            {
                min_fac = fac;
                min_load_loop = loop;
            }
        }
    }

    return *min_load_loop;
}

void event_context_t::add_event_loop(event_loop_t *loop)
{
    std::unique_lock<std::shared_mutex> lock(loop_mutex);
    loop->set_context(this);
    event_demultiplexer *demuxer = nullptr;
    switch (strategy)
    {
        case event_strategy::select:
            demuxer = new event_select_demultiplexer();
            break;
        case event_strategy::epoll:
            demuxer = new event_epoll_demultiplexer();
            break;
        case event_strategy::IOCP:
        default:
            throw std::invalid_argument("invalid strategy");
    }
    loop->set_demuxer(demuxer);
    loops.push_back(loop);
}

void event_context_t::remove_event_loop(event_loop_t *loop)
{
    std::unique_lock<std::shared_mutex> lock(loop_mutex);
    auto it = std::find(loops.begin(), loops.end(), loop);
    if (it != loops.end())
    {
        delete loop->get_demuxer();
        loops.erase(it);
    }
}

} // namespace net
