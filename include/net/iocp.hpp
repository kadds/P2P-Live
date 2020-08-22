/**
* \file iocp.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief iocp demultiplexer implementation
* \version 0.1
* \date 2020-08-22
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
#include "event.hpp"
#include "net.hpp"
#ifdef OS_WINDOWS

namespace net
{
class event_iocp_demultiplexer : public event_demultiplexer
{
    int handle;

  public:
    event_iocp_demultiplexer();
    ~event_iocp_demultiplexer();
    void add(handle_t handle, event_type_t type) override;
    handle_t select(event_type_t *type, microsecond_t *timeout) override;
    void remove(handle_t handle, event_type_t type) override;
};
} // namespace net
#endif