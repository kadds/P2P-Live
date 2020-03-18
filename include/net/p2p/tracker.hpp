/**
* \file tracker.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief tracker server and tracker node client
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
#include "../endian.hpp"
#include "../net.hpp"
#include "../rudp.hpp"
#include "../tcp.hpp"
#include <functional>
#include <unordered_set>

namespace net::p2p
{

enum class request_strategy : u8
{
    random,
    min_workload,
    edge_node,
};
/// tracker packet type
namespace tracker_packet
{
enum
{
    ping = 1,
    pong,
    get_tracker_info_request,
    get_tracker_info_respond,
    get_nodes_request,
    get_nodes_respond,
    get_trackers_request,
    get_trackers_respond,
    init_connection,

    peer_request,
    heartbeat = 0xFF,
};
}

#pragma pack(push, 1)

/// type 1  2
struct tracker_ping_pong_t
{
    u32 peer_workload;
    u32 tracker_neighbor_count;
    /// tcp server bind port
    u16 port;
    u16 udp_port;
    using member_list_t = serialization::typelist_t<u32, u32, u16, u16>;
};

struct tracker_peer_node_t
{
    u16 port;
    u16 udp_port;
    u32 ip;
    using member_list_t = serialization::typelist_t<u16, u32>;
};

struct tracker_tracker_node_t
{
    u16 port;
    u16 udp_port;
    u32 ip;
    using member_list_t = serialization::typelist_t<u16, u32>;
};

struct get_tracker_info_request_t
{
    using member_list_t = serialization::typelist_t<>;
};

struct get_tracker_info_respond_t
{
    u16 tudp_port;
    u16 cport;
    u32 cip;
    using member_list_t = serialization::typelist_t<u16, u16, u32>;
};

struct init_connection_t
{
    u64 register_sid; /// key must be set when sid is 0 (present edge server)
    u8 key[512];
    using member_list_t = serialization::typelist_t<u64, u8[512]>;
};

struct get_nodes_request_t
{
    u16 max_count;
    u64 sid;
    request_strategy strategy;
    using member_list_t = serialization::typelist_t<u16, u64, u8>;
};

struct get_nodes_respond_t
{
    u16 return_count;
    u32 available_count;
    u64 sid;
    tracker_peer_node_t peers[0];
    using member_list_t = serialization::typelist_t<u16, u32, u64>;
};

struct get_trackers_request_t
{
    u16 max_count;
    using member_list_t = serialization::typelist_t<u16>;
};

struct get_trackers_respond_t
{
    u16 return_count;
    u32 available_count;
    tracker_tracker_node_t trackers[0];
    using member_list_t = serialization::typelist_t<u16, u32>;
};

struct tracker_heartbeat_t
{
    using member_list_t = serialization::typelist_t<>;
};

constexpr u64 conn_request_magic = 0xC0FF8888;
struct udp_connection_request_t
{
    u32 magic; // conn_request_magic
    u16 target_port;
    u32 target_ip;
    u16 from_port;
    u32 from_ip;
    u16 from_udp_port;
    u64 sid;
    using member_list_t = serialization::typelist_t<u32, u16, u32, u16, u32, u16, u64>;
};

#pragma pack(pop)

struct peer_node_t
{
    u16 port;
    u16 udp_port;
    u32 ip;
    peer_node_t(){};
    peer_node_t(u16 port, u16 udp_port, u32 ip)
        : port(port)
        , udp_port(udp_port)
        , ip(ip)
    {
    }
};

struct tracker_node_t
{
    u16 port;
    u16 udp_port;
    u32 ip;
    tracker_node_t(){};
    tracker_node_t(u16 port, u16 udp_port, u32 ip)
        : port(port)
        , udp_port(udp_port)
        , ip(ip)
    {
    }
};

struct tracker_info_t
{
    microsecond_t last_ping;
    u32 workload;
    u32 trackers;
    /// remote
    tracker_node_t node;

    tcp::client_t client;
    tcp::connection_t conn_server;
    bool is_client;
    bool closed;
    tracker_info_t()
        : last_ping(0)
        , workload(0)
        , trackers(0)
        , conn_server(nullptr)
        , is_client(false)
        , closed(false)
    {
    }
};

struct node_info_t
{
    microsecond_t last_ping;
    u32 workload;
    peer_node_t node;
    u64 sid;
    tcp::connection_t conn;

    node_info_t()
        : last_ping(0)
        , workload(0)
        , conn(nullptr){};
};

struct addr_hash_func
{
    u64 operator()(const socket_addr_t &addr) const { return addr.hash(); }
};

// 30s
constexpr static inline u64 node_tick_timespan = 30000000;
constexpr static inline u64 node_tick_times = 2;

/// BUG: there is not thread-safety, try fix it.
class tracker_server_t
{
  private:
    using link_error_handler_t = std::function<void(tracker_server_t &, socket_addr_t address, connection_state)>;
    using link_handler_t = std::function<void(tracker_server_t &, socket_addr_t address)>;

    using peer_add_handler_t = std::function<void(tracker_server_t &, peer_node_t node, u64 sid)>;
    using peer_remove_handler_t = std::function<void(tracker_server_t &, peer_node_t node, u64 sid)>;
    using peer_error_handler_t = std::function<void(tracker_server_t &, socket_addr_t, u64 sid, connection_state)>;

    using peer_connect_handler_t = std::function<void(tracker_server_t &, peer_node_t node)>;

  private:
    /// 1min
    constexpr static inline u64 tick_timespan = 60000000;
    constexpr static inline u64 tick_times = 2;

    tcp::server_t server;
    rudp_t udp;
    u16 udp_port;
    std::string edge_key;

    /// save index of tracker_infos
    std::unordered_map<socket_addr_t, std::unique_ptr<tracker_info_t>, addr_hash_func> trackers;
    std::unordered_map<socket_addr_t, size_t, addr_hash_func> nodes;
    std::vector<node_info_t> node_infos;
    link_error_handler_t link_error_handler;
    link_handler_t link_handler, unlink_handler;
    peer_add_handler_t add_handler;
    peer_remove_handler_t remove_handler;
    peer_error_handler_t peer_error_handler;
    peer_connect_handler_t normal_peer_connect_handler;

  private:
    void server_main(tcp::connection_t conn);
    void client_main(tcp::connection_t conn);
    void udp_main(rudp_connection_t conn);
    void update_tracker(socket_addr_t addr, tcp::connection_t conn, tracker_ping_pong_t &res);

  public:
    tracker_server_t(){};
    tracker_server_t(const tracker_server_t &) = delete;
    tracker_server_t &operator=(const tracker_server_t &) = delete;

    ~tracker_server_t();

    void config(std::string edge_key);

    void bind(event_context_t &context, socket_addr_t addr, int max_client_count, bool reuse_addr = false);
    void link_other_tracker_server(event_context_t &context, socket_addr_t addr, microsecond_t timeout);
    tracker_server_t &on_link_error(link_error_handler_t handler);
    tracker_server_t &on_link_server(link_handler_t handler);
    tracker_server_t &on_unlink_server(link_handler_t handler);

    tracker_server_t &on_shared_peer_add_connection(peer_add_handler_t handler);
    tracker_server_t &on_shared_peer_remove_connection(peer_remove_handler_t handler);
    tracker_server_t &on_shared_peer_error(peer_error_handler_t handler);

    tracker_server_t &on_normal_peer_connect(peer_connect_handler_t handler);

    std::vector<tracker_node_t> get_trackers() const;

    void close();
};

/// client under NAT or not
class tracker_node_client_t
{
  private:
    using nodes_update_handler_t = std::function<void(tracker_node_client_t &, peer_node_t *, u64)>;
    using trackers_update_handler_t = std::function<void(tracker_node_client_t &, tracker_node_t *, u64)>;
    using nodes_connect_handler_t = std::function<void(tracker_node_client_t &, peer_node_t)>;
    using error_handler_t = std::function<void(tracker_node_client_t &, socket_addr_t addr, connection_state)>;
    using disconnect_handler_t = std::function<void(tracker_node_client_t &)>;
    using tracker_connect_handler_t = std::function<void(tracker_node_client_t &, socket_addr_t)>;

  private:
    tcp::client_t client;

    socket_addr_t server_udp_address;

    u16 client_rudp_port;
    u16 client_outer_port;
    u32 client_outer_ip;

    nodes_update_handler_t node_update_handler;
    trackers_update_handler_t tracker_update_handler;
    nodes_connect_handler_t connect_handler;
    error_handler_t error_handler;
    tracker_connect_handler_t tracker_connect_handler;
    u64 sid;
    microsecond_t timeout;
    bool request_trackers;
    bool is_peer_client;
    bool wait_next_package;

    std::queue<std::tuple<int, request_strategy>> node_queue;
    socket_addr_t remote_server_address;
    event_context_t *context;
    std::string key;

  private:
    void main(tcp::connection_t conn);
    void update_trackers(int count);
    void update_nodes();

  public:
    tracker_node_client_t(){};
    tracker_node_client_t(const tracker_node_client_t &) = delete;
    tracker_node_client_t &operator=(const tracker_node_client_t &) = delete;

    void config(bool as_peer_server, u64 sid, std::string key);

    void connect_server(event_context_t &context, socket_addr_t addr, microsecond_t timeout);

    void request_update_trackers();

    ///\note the nodes can contain self
    tracker_node_client_t &on_nodes_update(nodes_update_handler_t handler);

    void request_update_nodes(int max_request_count, request_strategy strategy);
    tracker_node_client_t &on_trackers_update(trackers_update_handler_t handler);

    void request_connect_node(peer_node_t node, rudp_t &udp);
    tracker_node_client_t &on_node_request_connect(nodes_connect_handler_t handler);

    tracker_node_client_t &on_error(error_handler_t handler);

    tracker_node_client_t &on_tracker_server_connect(tracker_connect_handler_t handler);

    socket_t *get_socket() const { return client.get_socket(); }

    void close();

    ~tracker_node_client_t();
};

} // namespace net::p2p
