/**
* \file epoll.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief epoll demultiplexer implementation
* \version 0.1
* \date 2020-03-13
*
* @copyright Copyright (c) 2020.
This file is part of P2P-Live.

P2P-Live is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

P2P-Live is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with P2P-Live. If not, see <http: //www.gnu.org/licenses/>.
*
*/

#pragma once
#ifndef OS_WINDOWS
#include "event.hpp"
#include <unordered_map>

namespace net
{
class event_epoll_demultiplexer : public event_demultiplexer
{
    int fd;
    int ev_fd;

  public:
    event_epoll_demultiplexer();
    ~event_epoll_demultiplexer();
    void add(handle_t handle, event_type_t type) override;
    handle_t select(event_type_t *type, microsecond_t *timeout) override;
    void remove(handle_t handle, event_type_t type) override;
    void wake_up(event_loop_t &cur_loop) override;
};
} // namespace net
#endif
