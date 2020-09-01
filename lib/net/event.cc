#include "net/event.hpp"
#include "net/co.hpp"
#include "net/epoll.hpp"
#include "net/execute_context.hpp"
#include "net/iocp.hpp"
#include "net/select.hpp"
#include "net/socket.hpp"
#include <algorithm>
#include <iostream>

namespace net
{
thread_local event_loop_t *thread_in_loop;

event_loop_t::event_loop_t(microsecond_t precision)
    : is_exit(false)
    , has_wake_up(false)
    , exit_code(0)
{
    time_manager = create_time_manager(precision);
    thread_in_loop = this;
#ifdef OS_WINDOWS
    HANDLE hThreadParent;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &hThreadParent, 0, FALSE,
                    DUPLICATE_SAME_ACCESS);
    handle = hThreadParent;
#endif
}

event_loop_t::~event_loop_t()
{
    thread_in_loop = nullptr;
#ifdef OS_WINDOWS
    CloseHandle(handle);
#endif
}

void event_loop_t::set_demuxer(event_demultiplexer *demuxer) { this->demuxer = demuxer; }

void event_loop_t::add_event_handler(handle_t handle, event_handler_t *handler)
{
    lock::lock_guard g(lock);
    event_map[handle] = handler;
}

void event_loop_t::remove_event_handler(handle_t handle, event_handler_t *handler)
{
    unlink(handle, event_type::error | event_type::writable | event_type::readable);

    lock::lock_guard g(lock);
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
            break;

        has_wake_up = false;
        auto handle = demuxer->select(&type, &timeout);
        if (handle != 0)
        {
            event_handle_map_t::iterator ev_it;
            {
                lock::lock_guard g(lock);
                ev_it = event_map.find(handle);
                if (ev_it == event_map.end())
                {
                    continue;
                }
            }
            auto handler = ev_it->second;
            handler->on_event(*context, type);
        }
        dispatcher.dispatch();
    }
    return exit_code;
}

void event_loop_t::exit(int code)
{
    exit_code = code;
    is_exit = true;
    demuxer->wake_up(*this);
}

void event_loop_t::wake_up()
{
    /// no need for wake in current thread
    if (this == thread_in_loop)
        return;
    if (!has_wake_up)
        demuxer->wake_up(*this);
    has_wake_up = true;
}

int event_loop_t::load_factor() { return (int)event_map.size(); }

event_loop_t &event_loop_t::current() { return *thread_in_loop; }

void event_context_t::add_executor(execute_context_t *exectx) { exectx->loop = &select_loop(); }

void event_context_t::add_executor(execute_context_t *exectx, event_loop_t *loop) { exectx->loop = loop; }

void event_context_t::remove_executor(execute_context_t *exectx) { exectx->loop = nullptr; }

timer_registered_t event_loop_t::add_timer(timer_t timer) { return time_manager->insert(timer); }

void event_loop_t::remove_timer(timer_registered_t reg) { time_manager->cancel(reg); }

execute_thread_dispatcher_t &event_loop_t::get_dispatcher() { return dispatcher; }

event_loop_t &event_context_t::select_loop()
{
    /// get minimum workload loop
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

void event_context_t::do_init()
{
    if (thread_in_loop == nullptr && !exit)
    {
        auto loop = new event_loop_t(precision);
        std::unique_lock<std::shared_mutex> lock(loop_mutex);
        if (exit)
        {
            return;
        }
        loop->set_context(this);
        event_demultiplexer *demuxer = nullptr;
        if (strategy == event_strategy::AUTO)
        {
#ifdef OS_WINDOWS
            strategy = event_strategy::IOCP;
#else
            strategy = event_strategy::epoll;
#endif
        }
        switch (strategy)
        {
            case event_strategy::select:
#ifdef OS_WINDOWS
                throw std::invalid_argument("not support select on windows");
#else
                demuxer = new event_select_demultiplexer();
#endif
                break;
            case event_strategy::epoll:
#ifdef OS_WINDOWS
                throw std::invalid_argument("not support epoll on windows");
#else
                demuxer = new event_epoll_demultiplexer();
#endif
                break;
            case event_strategy::IOCP:
#ifndef OS_WINDOWS
                throw std::invalid_argument("not support epoll on unix/linux");
#else
                if (iocp_handle == 0)
                {
                    iocp_handle = event_iocp_demultiplexer::make();
                }
                demuxer = new event_iocp_demultiplexer(iocp_handle);
#endif
                break;
            default:
                throw std::invalid_argument("invalid strategy " + std::to_string(strategy));
        }
        loop->set_demuxer(demuxer);
        loops.push_back(loop);
        loop_counter++;
    }
}

event_context_t::event_context_t(event_strategy strategy, microsecond_t precision)
    : strategy(strategy)
    , loop_counter(0)
    , precision(precision)
    , exit(false)
{
#ifdef OS_WINDOWS
    iocp_handle = 0;
#endif
    do_init();
}

int event_context_t::run()
{
    do_init();
    if (thread_in_loop == nullptr)
        return 0;

    int code = thread_in_loop->run();

    loop_counter--;
    std::unique_lock<std::mutex> lock(exit_mutex);
    cond.notify_all();
    cond.wait(lock, [this]() { return loop_counter == 0; });

    return code;
}

int event_context_t::prepare()
{
    do_init();
    return 0;
}

void event_context_t::exit_all(int code)
{
    exit = true;
    std::unique_lock<std::shared_mutex> lock(loop_mutex);
    for (auto &loop : loops)
    {
        loop->exit(code);
    }
}

event_context_t::~event_context_t()
{
    std::unique_lock<std::shared_mutex> lock(loop_mutex);
    for (auto loop : loops)
    {
        delete loop->get_demuxer();
        delete loop;
    }
#ifdef OS_WINDOWS
    if (iocp_handle != 0)
    {
        event_iocp_demultiplexer::close(iocp_handle);
        iocp_handle = 0;
    }
#endif
}

} // namespace net
