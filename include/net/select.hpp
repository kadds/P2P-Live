#pragma once
#include "event.hpp"
#include <unordered_map>

namespace net
{
class event_select_demultiplexer : public event_demultiplexer
{
    fd_set read_set;
    fd_set write_set;
    fd_set error_set;

  public:
    event_select_demultiplexer();
    ~event_select_demultiplexer();
    void add(handle_t handle, event_type_t type) override;
    handle_t select(event_type_t *type) override;
    void remove(handle_t handle, event_type_t type) override;
};

} // namespace net