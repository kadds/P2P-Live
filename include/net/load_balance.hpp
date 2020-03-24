/**
* \file load_balance.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief Four-tier load balancing server implementation
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
#include "endian.hpp"
#include "net.hpp"
#include "tcp.hpp"
#include "timer.hpp"
#include <functional>
#include <unordered_map>

/// TODO: load balancing server, not finish yet. Four level load balancing.
namespace net
{
#pragma pack(push, 1)

struct peer_get_server_request_t
{
    u16 version;
    u32 room_id;
    u32 key;
    using member_list_t = net::serialization::typelist_t<u16, u32, u32>;
};

struct peer_get_server_respond_t
{
    u16 version;
    u16 port;
    u32 ip_addr;
    u32 session_id;
    u8 state;
    using member_list_t = net::serialization::typelist_t<u16, u16, u32, u32, u8>;
};

struct pull_server_request_common_t
{
    u16 type;
    using member_list_t = net::serialization::typelist_t<u16>;
};

struct pull_inner_server_respond_t
{
    u32 connect_count;
    using member_list_t = net::serialization::typelist_t<u32>;
};

#pragma pack(pop)
namespace load_balance
{

/// load balance server
/// context server connect it that sending server work load, address
/// peer client connect it to get the context server address
class front_server_t
{
  public:
    using server_join_handler_t = std::function<void(front_server_t &, tcp::connection_t)>;
    using front_handler_t = std::function<bool(front_server_t &, const peer_get_server_request_t &,
                                               peer_get_server_respond_t &, tcp::connection_t)>;

  private:
    front_handler_t handler;
    server_join_handler_t server_handler;
    tcp::server_t server, inner_server;

  public:
    /// bind tcp acceptor. recvice inner server data.
    ///
    void bind_inner(event_context_t &context, socket_addr_t addr, bool reuse_addr = false);

    void bind(event_context_t &context, socket_addr_t addr, bool reuse_addr = false);
    front_server_t &on_client_request(front_handler_t handler);
    front_server_t &on_inner_server_join(server_join_handler_t handler);
};

class front_client_t
{
    tcp::client_t client;
};

} // namespace load_balance
} // namespace net
