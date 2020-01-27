#pragma once
#include "event.hpp"

namespace net
{
class event_epoll_demultiplexer : public event_demultiplexer
{
    int fd;

  public:
    event_epoll_demultiplexer();
    void add(socket_t socket, event_type type) override;
    socket_t select(event_type *type) override;
    void remove(socket_t socket, event_type type) override;
};
}