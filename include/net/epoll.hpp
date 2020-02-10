#pragma once
#include "event.hpp"
#include <unordered_map>

namespace net
{
class event_epoll_demultiplexer : public event_demultiplexer
{
    int fd;

  public:
    event_epoll_demultiplexer();
    ~event_epoll_demultiplexer();
    void add(handle_t handle, event_type_t type) override;
    handle_t select(event_type_t *type, microsecond_t *timeout) override;
    void remove(handle_t handle, event_type_t type) override;
};
} // namespace net