#include "net/epoll.hpp"
#include <sys/epoll.h>
namespace net
{
event_epoll_demultiplexer::event_epoll_demultiplexer() { fd = ::epoll_create(100); }

void event_epoll_demultiplexer::add(socket_t socket, event_type type)
{
    epoll_event ev;
    switch (type)
    {
        case event_type::readable:
            ev.events = EPOLLIN;
            break;
        case event_type::writable:
            ev.events = EPOLLOUT;
            break;
        case event_type::error:
            ev.events = EPOLLERR;
            break;
        default:
            break;
    }
    ev.events |= EPOLLET;
    ev.data.fd = socket.get_raw_handle();
    epoll_ctl(fd, EPOLL_CTL_ADD, socket.get_raw_handle(), &ev);
}

socket_t event_epoll_demultiplexer::select(event_type *type)
{
    epoll_event ev;
    ::epoll_wait(fd, &ev, 1, 0);
    if (ev.events & EPOLLIN)
    {
        *type = event_type::readable;
        return ev.data.fd;
    }
    else if (ev.events & EPOLLOUT)
    {
        *type = event_type::writable;
        return ev.data.fd;
    }
    else if (ev.events & EPOLLERR)
    {
        *type = event_type::error;
        return ev.data.fd;
    }
}

void event_epoll_demultiplexer::remove(socket_t socket, event_type type)
{
    epoll_event ev;
    switch (type)
    {
        case event_type::readable:
            ev.events = EPOLLIN;
            break;
        case event_type::writable:
            ev.events = EPOLLOUT;
            break;
        case event_type::error:
            ev.events = EPOLLERR;
            break;
        default:
            break;
    }
    ev.data.fd = socket.get_raw_handle();
    epoll_ctl(fd, EPOLL_CTL_DEL, socket.get_raw_handle(), &ev);
}

} // namespace net
