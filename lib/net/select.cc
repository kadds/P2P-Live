#include "net/select.hpp"

namespace net
{
event_select_demultiplexer::event_select_demultiplexer()
{
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    FD_ZERO(&error_set);
}

void event_select_demultiplexer::add(socket_t socket, event_type type)
{
    switch (type)
    {
        case event_type::readable:
            FD_SET(socket.get_raw_handle(), &read_set);
            break;
        case event_type::writable:
            FD_SET(socket.get_raw_handle(), &write_set);
            break;
        case event_type::error:
            FD_SET(socket.get_raw_handle(), &error_set);
            break;
        default:
            break;
    }
}

socket_t event_select_demultiplexer::select(event_type *type)
{
    fd_set rs = read_set;
    fd_set ws = write_set;
    fd_set es = error_set;

    ::select(FD_SETSIZE, &rs, &ws, &es, 0);
    for (int i = 0; i < FD_SETSIZE; i++)
    {
        if (FD_ISSET(i, &rs))
        {
            *type = event_type::readable;
            return i;
        }
        else if (FD_ISSET(i, &ws))
        {
            *type = event_type::writable;
            return i;
        }
        else if (FD_ISSET(i, &es))
        {
            *type = event_type::error;
            return i;
        }
    }
}

void event_select_demultiplexer::remove(socket_t socket, event_type type)
{
    switch (type)
    {
        case event_type::readable:
            FD_CLR(socket.get_raw_handle(), &read_set);
            break;
        case event_type::writable:
            FD_CLR(socket.get_raw_handle(), &write_set);
            break;
        case event_type::error:
            FD_CLR(socket.get_raw_handle(), &error_set);
            break;
        default:
            break;
    }
}

} // namespace net
