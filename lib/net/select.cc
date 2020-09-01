#include "net/select.hpp"
#ifndef OS_WINDOWS
namespace net
{
event_select_demultiplexer::event_select_demultiplexer()
{
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    FD_ZERO(&error_set);
    fd = eventfd(1, 0);
    add(fd, event_type::readable);
}

event_select_demultiplexer::~event_select_demultiplexer() {}

/// XXX: the event will be available when call next 'select'. it must be in this thread, so the problem will not occur
void event_select_demultiplexer::add(handle_t handle, event_type_t type)
{
    if (type & event_type::readable)
        FD_SET(handle, &read_set);
    if (type & event_type::writable)
        FD_SET(handle, &write_set);
    if (type & event_type::error)
        FD_SET(handle, &error_set);
} // namespace net

handle_t event_select_demultiplexer::select(event_type_t *type, microsecond_t *timeout)
{
    fd_set rs = read_set;
    fd_set ws = write_set;
    fd_set es = error_set;
    struct timeval t;
    t.tv_sec = *timeout / 1000000;
    t.tv_usec = *timeout % 1000000;
    /// 0 1 2 is STDIN STDOUT STDERR in linux
    auto ret = ::select(FD_SETSIZE + 1, &rs, &ws, &es, &t);
    if (ret == -1)
        return 0;
    if (ret == 0)
    {
        *timeout = 0;
        return 0;
    }
    for (int i = 3; i < FD_SETSIZE; i++)
    {
        int j = i;
        if (FD_ISSET(j, &rs))
        {
            *type = event_type::readable;
        }
        else if (FD_ISSET(j, &ws))
        {
            *type = event_type::writable;
        }
        else if (FD_ISSET(j, &es))
        {
            *type = event_type::error;
        }
        else
        {
            continue;
        }
        if (j == fd)
        {
            // wake up
            eventfd_t vl;
            eventfd_read(fd, &vl);
            *timeout = 0;
            return 0;
        }
        return j;
    }
    return 0;
}

void event_select_demultiplexer::remove(handle_t handle, event_type_t type)
{
    if (type & event_type::readable)
        FD_CLR(handle, &read_set);
    if (type & event_type::writable)
        FD_CLR(handle, &write_set);
    if (type & event_type::error)
        FD_CLR(handle, &error_set);
}

void event_select_demultiplexer::wake_up(event_loop_t &cur_loop) { eventfd_write(fd, 1); }

} // namespace net

#endif