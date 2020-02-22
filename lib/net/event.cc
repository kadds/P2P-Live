#include "net/event.hpp"
#include "net/co.hpp"
#include "net/epoll.hpp"
#include "net/execute_context.hpp"
#include "net/select.hpp"
#include "net/socket.hpp"
#include <algorithm>
#include <iostream>
#include <signal.h>
#include <sys/eventfd.h>

namespace net
{
thread_local event_loop_t *thread_in_loop;

class event_fd_handler_t : public event_handler_t
{
    int fd;

  public:
    event_fd_handler_t() { fd = eventfd(1, 0); }
    ~event_fd_handler_t() { close(fd); }

    int get_fd() const { return fd; }

    void on_event(event_context_t &, event_type_t type) override
    {
        eventfd_t vl;
        eventfd_read(fd, &vl);
    }

    void write() const { eventfd_write(fd, 1); }
};

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

void event_loop_t::set_demuxer(event_demultiplexer *demuxer)
{
    this->demuxer = demuxer;
    wake_up_event_handler = std::make_unique<event_fd_handler_t>();
    auto fd = wake_up_event_handler->get_fd();
    add_event_handler(fd, wake_up_event_handler.get());

    demuxer->add(fd, event_type::readable);
}

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
    wake_up();
}

void event_loop_t::wake_up()
{
    if (this == thread_in_loop)
        return;
    wake_up_event_handler->write();
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

event_context_t::event_context_t(event_strategy strategy)
    : strategy(strategy)
{
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

void event_context_t::exit_all_loop(int code)
{
    for (auto &loop : loops)
    {
        loop->exit(code);
    }
}

} // namespace net
