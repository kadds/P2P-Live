#include "net/epoll.hpp"
#include "net/net_exception.hpp"
#include <sys/epoll.h>
namespace net
{
event_epoll_demultiplexer::event_epoll_demultiplexer()
{
    fd = ::epoll_create(100);
    if (fd <= 0)
    {
        throw net_param_exception("epoll create failed!");
    }
}

event_epoll_demultiplexer::~event_epoll_demultiplexer() { close(fd); }

void event_epoll_demultiplexer::add(handle_t handle, event_type_t type)
{
    int e = 0;
    if (type & event_type::readable)
        e |= EPOLLIN;
    if (type & event_type::writable)
        e |= EPOLLOUT;
    if (type & event_type::error)
        e |= EPOLLERR;

    epoll_event ev;
    ev.events = e | EPOLLET;
    ev.data.fd = handle;
    epoll_ctl(fd, EPOLL_CTL_ADD, handle, &ev);
}

handle_t event_epoll_demultiplexer::select(event_type_t *type)
{
    epoll_event ev;
    int c = ::epoll_wait(fd, &ev, 1, -1);
    if (c <= 0)
    {
        return 0;
    }
    int fd = 0;
    if (ev.events & EPOLLIN)
    {
        *type = event_type::readable;
        fd = ev.data.fd;
    }
    else if (ev.events & EPOLLOUT)
    {
        *type = event_type::writable;
        fd = ev.data.fd;
    }
    else if (ev.events & EPOLLERR)
    {
        *type = event_type::error;
        fd = ev.data.fd;
    }
    else
    {
        return 0;
    }
    return ev.data.fd;
}

void event_epoll_demultiplexer::remove(handle_t handle, event_type_t type)
{
    int e = 0;
    if (type & event_type::readable)
        e |= EPOLLIN;
    if (type & event_type::writable)
        e |= EPOLLOUT;
    if (type & event_type::error)
        e |= EPOLLERR;

    epoll_event ev;
    ev.events = e;
    ev.data.fd = handle;
    epoll_ctl(fd, EPOLL_CTL_DEL, handle, &ev);
}

} // namespace net
