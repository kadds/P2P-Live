#pragma once
#include "../endian.hpp"
#include "../net.hpp"
#include "../rudp.hpp"
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
    min_workload,
    edge_node,
};

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
    u64 register_sid;
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
    u32 magic;
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

class tracker_server_t
{
  private:
    using error_handler_t = std::function<void(tracker_server_t &, socket_addr_t address, connection_state)>;
    using link_handler_t = std::function<void(tracker_server_t &, socket_addr_t address)>;

  private:
    /// 1min
    constexpr static inline u64 tick_timespan = 60000000;
    constexpr static inline u64 tick_times = 2;

    tcp::server_t server;
    rudp_t udp;
    u16 udp_port;
    std::string edge_key;

    void server_main(tcp::connection_t conn);
    void client_main(tcp::connection_t conn);
    void udp_main(rudp_connection_t conn);

    /// save index of tracker_infos
    std::unordered_map<socket_addr_t, std::unique_ptr<tracker_info_t>, addr_hash_func> trackers;
    std::unordered_map<socket_addr_t, size_t, addr_hash_func> nodes;
    std::vector<node_info_t> node_infos;
    error_handler_t error_handler;
    link_handler_t link_handler, unlink_handler;

    void update_tracker(socket_addr_t addr, tcp::connection_t conn, tracker_ping_pong_t &res);

  public:
    tracker_server_t(){};
    tracker_server_t(const tracker_server_t &) = delete;
    tracker_server_t &operator=(const tracker_server_t &) = delete;

    ~tracker_server_t();

    void config(std::string edge_key);

    void bind(event_context_t &context, socket_addr_t addr, bool reuse_addr = false);
    void link_other_tracker_server(event_context_t &context, socket_addr_t addr, microsecond_t timeout);
    tracker_server_t &on_link_error(error_handler_t handler);
    tracker_server_t &on_link_server(link_handler_t handler);
    tracker_server_t &on_unlink_server(link_handler_t handler);

    std::vector<tracker_node_t> get_trackers() const;

    void close();
};

/// client under NAT or not
/// sid == 0 is edge server
class tracker_node_client_t
{
  private:
    using nodes_update_handler_t = std::function<void(tracker_node_client_t &, peer_node_t *, u64)>;
    using trackers_update_handler_t = std::function<void(tracker_node_client_t &, tracker_node_t *, u64)>;
    using nodes_connect_handler_t = std::function<void(tracker_node_client_t &, peer_node_t, u16 udp_port)>;
    using error_handler_t = std::function<void(tracker_node_client_t &, socket_t *, connection_state)>;
    using disconnect_handler_t = std::function<void(tracker_node_client_t &)>;

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

    u64 sid;
    microsecond_t timeout;
    bool request_trackers;
    bool is_peer_client;
    bool wait_next_package;

    std::queue<std::tuple<int, request_strategy>> node_queue;

    void main(tcp::connection_t conn);
    void update_trackers(int count);
    void update_nodes();

    socket_addr_t remote_server_address;

    event_context_t *context;

    std::string key;

  public:
    tracker_node_client_t(){};
    tracker_node_client_t(const tracker_node_client_t &) = delete;
    tracker_node_client_t &operator=(const tracker_node_client_t &) = delete;

    void config(bool as_peer_server, u64 sid, std::string key);

    void connect_server(event_context_t &context, socket_addr_t addr, microsecond_t timeout);

    void request_update_trackers();
    tracker_node_client_t &on_nodes_update(nodes_update_handler_t handler);

    void request_update_nodes(int max_request_count, request_strategy strategy);
    tracker_node_client_t &on_trackers_update(trackers_update_handler_t handler);

    void request_connect_node(peer_node_t node, rudp_t &udp);
    tracker_node_client_t &on_node_request_connect(nodes_connect_handler_t handler);

    tracker_node_client_t &on_error(error_handler_t handler);

    socket_t *get_socket() const { return client.get_socket(); }

    void close();

    ~tracker_node_client_t();
};

} // namespace net::p2p
