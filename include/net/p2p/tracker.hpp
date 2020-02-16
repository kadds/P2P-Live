#pragma once
#include "../endian.hpp"
#include "../net.hpp"
#include "../tcp.hpp"
#include <functional>
#include <unordered_set>

namespace net::p2p
{

/**
 * tracker server
 * 1. bind tcp server. link trackers and get request from peer nodes
 * 2. bind rudp server to detect NAT.
 *
 * peer A startup
 * if A as a peer server.
 *      A connect to tracker server. keep long tcp connection.
 *          A->server
 *          tracker_heartbeat_t
 *
 *
 * A get peer nodes
 * 1. A request peer nodes from tracker server.
 *      A->server
 *      get_nodes_request_t
 * 2. tracker server send peer nodes to A.
 *      server->A
 *      get_nodes_respond_t
 *
 * A get trackers
 * 1. A request trackers from tracker server.
 *      A->server
 *      get_trackers_request_t
 * 2. tracker server send data to A
 *      server->A
 *      get_trackers_respond_t
 *
 * When A need connect to a peer server B. (A | NAT, B | NAT)
 * 1. A bind an rudp port.
 * 2. send detect package to tracker server from rudp port. request nodes connection to tracker server to tracker
 * server.
 *      A->server
 *      connection_request_t
 * 3. tracker server send A connection info (A's rudp ip, port) to B.
 *      server->B (long connection) forward
 *      connection_request_t
 *
 * 4. B bind port, send detect package to tracker server.
 *      B->server
 *      connection_request_t
 * 5. B try send udp to A rudp address
 *      B->A
 *      connection_request_t
 *
 * 6. tracker server forward B package to A
 *      server->A
 *      connection_request_t
 *
 * 7. A connect B
 *      A->B
 *      connection_request_t
 *
 *
 */

enum class request_strategy : u8
{
    random,
    min_load_work,
};

namespace tracker_packet
{
enum
{
    ping = 1,
    pong,
    get_nodes_request,
    get_nodes_respond,
    get_trackers_request,
    get_trackers_respond,
    heartbeat = 0xFF,
};
}

#pragma pack(push, 1)

/// type 1  2
struct tracker_ping_pong_t
{
    /// server bind port
    /// not used now
    u16 port;
    /// not used now
    u32 ip;
    u32 peer_workload;
    u32 tracker_neighbor_count;
    using member_list_t = serialization::typelist_t<u16, u32, u32, u32>;
};

struct peer_node_t
{
    u16 port;
    u32 ip;
    using member_list_t = serialization::typelist_t<u16, u32>;
};

struct tracker_node_t
{
    u16 port;
    u32 ip;
    using member_list_t = serialization::typelist_t<u16, u32>;
};

/// type 3
struct get_nodes_request_t
{
    u32 max_count;
    u64 sid;
    request_strategy strategy;
    using member_list_t = serialization::typelist_t<u32, u64, u8>;
};

/// type 4
struct get_nodes_respond_t
{
    u32 return_count;
    u32 available_count;
    u64 sid;
    peer_node_t peers[0];
    using member_list_t = serialization::typelist_t<u32, u32, u64>;
};

/// type 5
struct get_trackers_request_t
{
    u32 max_count;
    request_strategy strategy;
    using member_list_t = serialization::typelist_t<u32, u8>;
};

/// type 6
struct get_trackers_respond_t
{
    u32 return_count;
    u32 available_count;
    tracker_node_t trackers[0];
    using member_list_t = serialization::typelist_t<u32, u32>;
};

/// type 0xFF
struct tracker_heartbeat_t
{
    using member_list_t = serialization::typelist_t<>;
};

struct connection_request_t
{
    u16 target_port;
    u32 target_ip;
    u16 local_port;
    u32 local_ip;
    u64 sid;
    using member_list_t = serialization::typelist_t<u16, u32, u16, u32, u64>;
};

#pragma pack(pop)

class tracker_server_t;

using tracker_client_join_handler_t = std::function<void(tracker_server_t &, tcp::connection_t)>;

struct hash_func
{
    u64 operator()(const socket_addr_t &addr) const { return addr.hash(); }
};

struct tracker_info_t
{
    /// remote
    socket_addr_t address;
    microsecond_t last_ping;
    u32 workload;
    u32 trackers;
    tcp::client_t client;
    tcp::connection_t conn_server;
    bool is_client;
    tracker_info_t()
        : last_ping(0)
        , workload(0)
        , trackers(0)
        , conn_server(nullptr)
        , is_client(false)
    {
    }
};

class tracker_server_t
{
    /// TODO: tracker server link timeout
    constexpr static inline u64 tick_timespan = 5000000;
    constexpr static inline u64 tick_times = 2;

    tcp::server_t server;

    void server_main(tcp::connection_t conn);
    void client_main(tcp::connection_t conn);

    std::unordered_map<socket_addr_t, std::unique_ptr<tracker_info_t>, hash_func> trackers;
    std::unordered_set<socket_addr_t, hash_func> nodes;

    void update_tracker(socket_addr_t addr, tcp::connection_t conn, tracker_ping_pong_t &res);

  public:
    tracker_server_t(){};
    void bind(event_context_t &context, socket_addr_t addr, bool reuse_addr = false);
    void link_other_tracker_server(event_context_t &context, socket_addr_t addr, microsecond_t timeout);
    std::vector<socket_addr_t> get_trackers() const;
};

class tracker_node_client_t;

using at_nodes_update_handler_t = std::function<void(tracker_node_client_t &, peer_node_t *nodes, u64 count)>;
using at_trackers_update_handler_t = std::function<void(tracker_node_client_t &, peer_node_t *nodes, u64 count)>;
using at_nodes_connect_handler_t = std::function<void(tracker_node_client_t &, peer_node_t *nodes, u64 count)>;

class tracker_node_client_t
{
    tcp::client_t client;
    at_nodes_update_handler_t update_handler;
    at_trackers_update_handler_t tracker_update_handler;
    at_nodes_connect_handler_t connect_handler;

  public:
    void connect_server(event_context_t &context, socket_addr_t addr, microsecond_t timeout);

    void request_update_nodes(int count, u64 sid, request_strategy strategy);
    tracker_node_client_t &at_nodes_update(at_nodes_update_handler_t handler);

    void request_update_trackers(int count, request_strategy strategy);
    tracker_node_client_t &at_trackers_update(at_trackers_update_handler_t handler);

    void request_connect_node(std::vector<socket_addr_t> addr);
    tracker_node_client_t &at_node_connectable(at_nodes_connect_handler_t handler);
};

} // namespace net::p2p
