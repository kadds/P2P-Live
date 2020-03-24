/**
* \file rudp.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief Reliable UDP implementation with KCP
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
#include "co.hpp"
#include "net.hpp"
#include "socket_addr.hpp"
#include "socket_buffer.hpp"
#include <functional>
#include <memory>

namespace net
{
class event_context_t;
class rudp_impl_t;
class socket_t;

struct rudp_connection_t
{
    socket_addr_t address;
    int channel;
};

class rudp_t
{
  public:
    using new_connection_handler_t = std::function<void(rudp_connection_t conn)>;

    /// return false will discard current packet
    using unknown_handler_t = std::function<bool(socket_addr_t address)>;
    using timeout_handler_t = std::function<void(rudp_connection_t)>;

  private:
    // impl idiom for third-party libraries
    rudp_impl_t *impl;

  public:
    rudp_t();
    ~rudp_t();

    rudp_t(const rudp_t &) = delete;
    rudp_t &operator=(const rudp_t &) = delete;

    /// bind a local address
    void bind(event_context_t &context, socket_addr_t local_addr, bool reuse_addr = false);

    /// bind random port
    void bind(event_context_t &context);

    /// addr remote address
    void add_connection(socket_addr_t addr, int channel, microsecond_t inactive_timeout);

    void add_connection(socket_addr_t addr, int channel, microsecond_t inactive_timeout,
                        std::function<void(rudp_connection_t)> co_func);

    /// level 0: faster. level 1: fast, level 2: slow
    void config(rudp_connection_t conn, int level);

    void set_wndsize(socket_addr_t addr, int channel, int send, int recv);

    rudp_t &on_new_connection(new_connection_handler_t handler);

    void remove_connection(socket_addr_t addr, int channel);

    void remove_connection(rudp_connection_t conn);

    bool removeable(socket_addr_t addr, int channel);

    /// is current connection removeable
    bool removeable(rudp_connection_t conn);

    rudp_t &on_unknown_packet(unknown_handler_t handler);

    rudp_t &on_connection_timeout(timeout_handler_t handler);

    co::async_result_t<io_result> awrite(co::paramter_t &param, rudp_connection_t conn, socket_buffer_t &buffer);
    co::async_result_t<io_result> aread(co::paramter_t &param, rudp_connection_t conn, socket_buffer_t &buffer);

    /// call func on connection context
    void run_at(rudp_connection_t conn, std::function<void()> func);

    socket_t *get_socket() const;

    void close_all_remote();

    int get_mtu() const
    {
        return 1472 - 24; // kcp header 24
    }

    void close();

    bool is_bind() const;
};

// wrapper functions
co::async_result_t<io_result> rudp_awrite(co::paramter_t &param, rudp_t *rudp, rudp_connection_t conn,
                                          socket_buffer_t &buffer);
co::async_result_t<io_result> rudp_aread(co::paramter_t &param, rudp_t *rudp, rudp_connection_t conn,
                                         socket_buffer_t &buffer);

} // namespace net
