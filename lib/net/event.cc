#include "net/event.hpp"
#include "net/epoll.hpp"
#include "net/select.hpp"
#include "net/socket.hpp"
#include <algorithm>

namespace net
{
event_context_t::event_context_t(event_strategy strategy)
    : strategy(strategy)
{
}

event_loop_t::event_loop_t()
    : is_exit(false)
    , exit_code(0)
{
}

void event_loop_t::add_socket(socket_t *socket) { socket_map[socket->get_raw_handle()] = socket; }

void event_loop_t::remove_socket(socket_t *socket)
{
    unlink(socket, event_type::error | event_type::writable | event_type::readable);

    auto it = socket_map.find(socket->get_raw_handle());
    if (it != socket_map.end())
        socket_map.erase(it);
}

event_loop_t &event_loop_t::link(socket_t *socket, event_type_t type)
{
    demuxer->add(socket->get_raw_handle(), type);
    return *this;
}

event_loop_t &event_loop_t::unlink(socket_t *socket, event_type_t type)
{
    demuxer->remove(socket->get_raw_handle(), type);
    return *this;
}

int event_loop_t::run()
{
    while (!is_exit)
    {
        event_type_t type;
        auto handle = demuxer->select(&type);

        auto socket_it = socket_map.find(handle);
        if (socket_it == socket_map.end())
            continue;
        auto socket = socket_it->second;
        socket->on_event(*context, type);
    }
    return exit_code;
}

void event_loop_t::exit(int code)
{
    exit_code = code;
    is_exit = true;
}

int event_loop_t::load_factor() { return socket_map.size(); }

event_loop_t &event_context_t::add_socket(socket_t *socket)
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
    {
        std::unique_lock<std::shared_mutex> slock(map_mutex);
        map[socket->get_raw_handle()] = min_load_loop;
    }
    min_load_loop->add_socket(socket);

    return *min_load_loop;
}

event_loop_t *event_context_t::remove_socket(socket_t *socket)
{
    event_loop_t *loop;
    {
        std::unique_lock<std::shared_mutex> slock(map_mutex);
        auto it = map.find(socket->get_raw_handle());
        if (it == map.end())
            return nullptr;
        loop = it->second;
        map.erase(it);
    }

    loop->remove_socket(socket);
    return loop;
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
        loops.erase(it);
}
} // namespace net