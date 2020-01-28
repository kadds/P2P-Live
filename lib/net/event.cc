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
    socket_map.erase(socket_map.find(socket->get_raw_handle()));
    unlink(socket, event_type::error | event_type::writable | event_type::readable);
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

int event_loop_t::load_factor() { return 1; }

event_loop_t &event_context_t::add_socket(socket_t *socket)
{
    std::shared_lock<std::shared_mutex> lock(loop_mutex);
    event_loop_t *min_load_loop = loops[0];
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

    min_load_loop->add_socket(socket);
    return *min_load_loop;
}

event_loop_t *event_context_t::remove_socket(socket_t *socket)
{
    std::shared_lock<std::shared_mutex> lock(loop_mutex);
    for (auto loop : loops)
    {
        loop->remove_socket(socket);
    }
    return nullptr;
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
    loops.erase(std::find(loops.begin(), loops.end(), loop));
}
} // namespace net