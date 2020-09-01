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

enum class io_type
{
    read,
    write,
    accept,
    wake_up,
    connect,
};

struct io_overlapped
{
    WSAOVERLAPPED overlapped;
    WSABUF wsaBuf;
    unsigned int buffer_do_len;
    sockaddr_in addr;
    int addr_len;
    int err;
    HANDLE sock;
    io_type type;
    void *data;
    bool done;
    io_overlapped();
};

class event_iocp_demultiplexer : public event_demultiplexer
{
    handle_t iocp_handle;
    std::unordered_map<handle_t, handle_t> map;

  public:
    static handle_t make();
    static void close(handle_t h);

    event_iocp_demultiplexer(handle_t);
    ~event_iocp_demultiplexer();
    void add(handle_t handle, event_type_t type) override;
    handle_t select(event_type_t *type, microsecond_t *timeout) override;
    void remove(handle_t handle, event_type_t type) override;
    void wake_up(event_loop_t &cur_loop) override;
};
} // namespace net
#endif