#pragma once
#include "../endian.hpp"
#include "../net.hpp"
#include "../tcp.hpp"
#include <functional>
#include <unordered_set>

namespace net::p2p
{
enum class get_node_strategy : u8
{
    random,
    min_load_work,
};

#pragma pack(push, 1)

struct tracker_ping_pong_t
{
    u8 timeout;
    u16 port;
    u32 ip;
    using member_list_t = serialization::typelist_t<u8, u8, u16, u32>;
};

struct peer_node_t
{
    u16 port;
    u32 ip;
    using member_list_t = serialization::typelist_t<u16, u32>;
};

struct get_node_request_t
{
    u16 count;
    u64 sid;
    get_node_strategy strategy;
    using member_list_t = serialization::typelist_t<u16, u64, u8>;
};

struct get_node_respond_t
{
    u16 count;
    u64 sid;
    peer_node_t peers[0];
    using member_list_t = serialization::typelist_t<u16, u64>;
};

#pragma pack(pop)

class tracker_server_t;

using tracker_ping_handler_t =
    std::function<void(tracker_server_t &, tracker_ping_pong_t *ping_pong, tcp::connection_t)>;

using tracker_client_join_handler_t = std::function<void(tracker_server_t &, tcp::connection_t)>;

struct hash_func
{
    u64 operator()(const socket_addr_t &addr) const { return addr.hash(); }
};

class tracker_server_t
{
    tcp::server_t server;
    tracker_ping_handler_t ping_handler;
    tracker_client_join_handler_t client_handler;

    void server_main(socket_t *socket);

    std::unordered_set<socket_addr_t, hash_func> trackers;
    std::unordered_set<socket_addr_t> nodes;

  public:
    tracker_server_t() = default;

    void bind(event_context_t &context, socket_addr_t addr, bool reuse_addr = false);

    tracker_server_t &at_tracker_ping(tracker_ping_handler_t handler);
    tracker_server_t &at_client_join(tracker_client_join_handler_t handler);
};

class tracker_node_client_t;

using at_nodes_update_handler_t = std::function<void(tracker_node_client_t &, peer_node_t *nodes, u64 count)>;

class tracker_node_client_t
{
    tcp::client_t client;
    at_nodes_update_handler_t update_handler;

  public:
    void connect(event_context_t &context, socket_addr_t addr);

    void request_update_nodes(int count, u64 sid, get_node_strategy strategy);
    tracker_node_client_t &at_nodes_update(at_nodes_update_handler_t handler);
};

} // namespace net::p2p
