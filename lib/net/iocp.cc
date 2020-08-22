#include "net/iocp.hpp"
#ifdef OS_WINDOWS

namespace net
{

event_iocp_demultiplexer::event_iocp_demultiplexer() {}

event_iocp_demultiplexer::~event_iocp_demultiplexer() {}

void event_iocp_demultiplexer::add(handle_t handle, event_type_t type) {} // namespace net

handle_t event_iocp_demultiplexer::select(event_type_t *type, microsecond_t *timeout) { return 0; }

void event_iocp_demultiplexer::remove(handle_t handle, event_type_t type) {}
} // namespace net
#endif