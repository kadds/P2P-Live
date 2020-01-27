#pragma once
#include "event.hpp"

namespace net
{
class event_select_demultiplexer : public event_demultiplexer
{
    fd_set read_set;
    fd_set write_set;
    fd_set error_set;

  public:
    event_select_demultiplexer();
    void add(socket_t socket, event_type type) override;
    socket_t select(event_type *type) override;
    void remove(socket_t socket, event_type type) override;
};

} // namespace net