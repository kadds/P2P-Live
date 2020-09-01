/**
* \file tcp.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief TCP wrapper. Including TCP package sending/recving, acceptor, connector.
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
#include "endian.hpp"
#include "socket_addr.hpp"
#include "socket_buffer.hpp"
#include "timer.hpp"
#include <functional>

namespace net
{
class event_context_t;
class socket_t;
}; // namespace net

namespace net::tcp
{
/// tcp application head
struct package_head_t
{
    u32 size; /// payload length
    using member_list_t = net::serialization::typelist_t<u32>;
};

class connection_t
{
    socket_t *socket;

  public:
    connection_t(socket_t *so)
        : socket(so){};
    /// async write data by stream mode
    co::async_result_t<io_result> awrite(co::paramter_t &param, socket_buffer_t &buffer);
    /// async read data by stream mode
    co::async_result_t<io_result> aread(co::paramter_t &param, socket_buffer_t &buffer);

    /// Wait for the next packet and read the tcp application header
    co::async_result_t<io_result> aread_packet_head(co::paramter_t &param, package_head_t &head,
                                                    socket_buffer_t &buffer);

    co::async_result_t<io_result> aread_packet_content(co::paramter_t &param, socket_buffer_t &buffer);

    /// write package
    co::async_result_t<io_result> awrite_packet(co::paramter_t &param, package_head_t &head, socket_buffer_t &buffer);

    socket_t *get_socket() { return socket; }
};

/// wrappers
co::async_result_t<io_result> conn_awrite(co::paramter_t &param, connection_t conn, socket_buffer_t &buffer);
co::async_result_t<io_result> conn_aread(co::paramter_t &param, connection_t conn, socket_buffer_t &buffer);
co::async_result_t<io_result> conn_aread_packet_head(co::paramter_t &param, connection_t conn, package_head_t &head);
co::async_result_t<io_result> conn_aread_packet_content(co::paramter_t &param, connection_t conn,
                                                        socket_buffer_t &buffer);
co::async_result_t<io_result> conn_awrite_packet(co::paramter_t &param, connection_t conn, package_head_t &head,
                                                 socket_buffer_t &buffer);

class server_t
{
  public:
    using handler_t = std::function<void(server_t &, connection_t)>;
    using error_handler_t = std::function<void(server_t &, socket_t *, socket_addr_t, connection_state)>;

  private:
    socket_t *server_socket;
    event_context_t *context;
    handler_t join_handler;
    handler_t exit_handler;
    error_handler_t error_handler;

  private:
    void wait_client();
    void client_main(socket_t *socket);

  public:
    server_t();
    ~server_t();
    /// listen port in acceptor coroutine.
    ///
    ///\param context event context
    ///\param address the address:port to bind
    ///\param max_wait_client client count in completion queue
    ///\param reuse_addr create socket by SO_REUSEADDR?
    void listen(event_context_t &context, socket_addr_t address, int max_wait_client, bool reuse_addr = false);

    server_t &on_client_join(handler_t handler);
    server_t &on_client_exit(handler_t handler);
    server_t &on_client_error(error_handler_t handler);

    void exit_client(socket_t *client);

    void close_server();

    socket_t *get_socket() const { return server_socket; }
};

class client_t
{
  public:
    using handler_t = std::function<void(client_t &, connection_t)>;
    using error_handler_t = std::function<void(client_t &, socket_t *, socket_addr_t, connection_state)>;

  private:
    socket_t *socket;
    socket_addr_t connect_addr;
    handler_t join_handler;
    handler_t exit_handler;
    error_handler_t error_handler;
    event_context_t *context;
    void wait_server(socket_addr_t address, microsecond_t timeout);

  public:
    client_t();
    ~client_t();
    /// connect to TCP server
    ///
    ///\param context event context
    ///\param server_address server tcp address to connect
    ///\param timeout timeout by microseconds
    ///\note call on_server_error when timeout
    void connect(event_context_t &context, socket_addr_t server_address, microsecond_t timeout);
    client_t &on_server_connect(handler_t handler);
    client_t &on_server_disconnect(handler_t handler);
    client_t &on_server_error(error_handler_t handler);

    void close();

    socket_addr_t get_connect_addr() const { return connect_addr; }
    socket_t *get_socket() const { return socket; }
    tcp::connection_t get_connection() const { return socket; }

    bool is_connect() const;
};

} // namespace net::tcp
