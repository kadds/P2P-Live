/**
* \file udp.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief Provide basic UDP sending/recving
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
#include "event.hpp"
#include "socket_addr.hpp"
#include "socket_buffer.hpp"
#include <functional>

namespace net
{
class socket_t;
};

namespace net::udp
{

class server_t
{
  private:
    socket_t *socket;
    event_context_t *context;

  public:
    ~server_t();
    socket_t *get_socket() const { return socket; }
    socket_t *bind(event_context_t &context, socket_addr_t addr, bool reuse_port = false);
    void run(std::function<void()> func);
    void close();
};

class client_t
{
  private:
    socket_t *socket;
    socket_addr_t connect_addr;
    event_context_t *context;

  public:
    ~client_t();
    socket_t *get_socket() const { return socket; }

    /// It just binds the socket to an event, not a real connection. Set remote_address_bind_to_socket to true to bind
    /// to the address
    void connect(event_context_t &context, socket_addr_t addr, bool remote_address_bind_to_socket = true);

    /// must call 'connect' befor call it.
    void run(std::function<void()> func);

    void close();
    socket_addr_t get_address() const;
};

} // namespace net::udp
