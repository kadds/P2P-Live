#include "net/event.hpp"
#include "net/epoll.hpp"
#include "net/select.hpp"
#include <algorithm>

namespace net
{
event_base_t::event_base_t(event_base_strategy strategy)
    : strategy(strategy)
    , is_exit(false)
    , exit_code(0)
{
    switch (strategy)
    {
        case event_base_strategy::select:
            demuxer = new event_select_demultiplexer();
            break;
        case event_base_strategy::epoll:
            demuxer = new event_epoll_demultiplexer();
            break;
        case event_base_strategy::IOCP:
        default:
            throw std::invalid_argument("invalid strategy");
    }
}

void event_base_t::add_handler(event_type type, socket_t socket, event_handler_t handler)
{
    {
        std::lock_guard<std::mutex> mtx(mutex);
        auto it = event_map.find(socket);
        if (it == event_map.end())
        {
            it = event_map.insert(std::make_pair(socket, socket_events_t())).first;
        }
        auto &type_map = it->second;
        auto it2 = type_map.find(type);
        if (it2 == type_map.end())
        {
            it2 = type_map.insert(std::make_pair(type, std::list<event_handler_t>())).first;
        }
        it2->second.push_back(handler);
        demuxer->add(socket, type);
    }
}

void event_base_t::remove_handler(event_type type, socket_t socket, event_handler_t handler)
{
    std::lock_guard<std::mutex> mtx(mutex);
    auto it = event_map.find(socket);
    if (it == event_map.end())
    {
        return;
    }
    auto &type_map = it->second;
    auto it2 = type_map.find(type);
    if (it2 == type_map.end())
    {
        return;
    }

    auto it3 = std::find(it2->second.begin(), it2->second.end(), handler);
    if (it3 == it2->second.end())
    {
        return;
    }
    demuxer->remove(socket, type);
    it2->second.erase(it3);
}

void event_base_t::remove_handler(event_type type, socket_t socket)
{
    std::lock_guard<std::mutex> mtx(mutex);
    auto it = event_map.find(socket);
    if (it == event_map.end())
    {
        return;
    }
    auto &type_map = it->second;
    auto it2 = type_map.find(type);
    if (it2 == type_map.end())
    {
        return;
    }
    demuxer->remove(socket, type);
    type_map.erase(it2);
}

void event_base_t::remove_handler(socket_t socket)
{
    std::lock_guard<std::mutex> mtx(mutex);
    auto it = event_map.find(socket);
    if (it == event_map.end())
    {
        return;
    }
    event_map.erase(it);

    demuxer->remove(socket, event_type::error);
    demuxer->remove(socket, event_type::readable);
    demuxer->remove(socket, event_type::writable);
}

void event_base_t::close_socket(socket_t socket) { close(socket.get_raw_handle()); }

int event_base_t::run()
{
    while (!is_exit)
    {
        event_type type;
        socket_t socket = demuxer->select(&type);
        auto so_it = event_map.find(socket);
        if (so_it == event_map.end())
            continue;

        auto type_it = so_it->second.find(type);
        if (type_it == so_it->second.end())
            continue;

        for (auto hander : type_it->second)
        {
            hander(*this, event_t(type, socket));
        }
    }
    return exit_code;
}

void event_base_t::exit(int code)
{
    exit_code = code;
    is_exit = true;
}
} // namespace net