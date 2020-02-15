#pragma once
#include "../endian.hpp"
#include "../net.hpp"
#include "../rudp.hpp"
#include "../socket_addr.hpp"
#include "../tcp.hpp"
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

/// forward
namespace net
{
class socket_t;
}

namespace net::p2p
{
using session_id_t = u64;
namespace peer_msg_type
{
enum : u8
{
    init_request = 0,
    init_respond,
    data_request,
    data_package,

    heart = 0xFF,
};
}

#pragma pack(push, 1)
/// send by client----------------------
// type = 0
struct peer_init_request_t
{
    u8 type;
    u64 sid;
    using member_list_t = serialization::typelist_t<u8, u64>;
};

// type = 2
struct peer_data_request_t
{
    u8 type;
    u8 priority;
    u64 data_id;
    using member_list_t = serialization::typelist_t<u8, u8, u64>;
};

// type = 0xFF 0xFE
struct peer_heart_t
{
    u8 type;
};

/// send by server----------------------
// type = 1
struct peer_init_respond_t
{
    u8 type;
    u64 first_data_id;
    u64 last_data_id;
    using member_list_t = serialization::typelist_t<u8, u64, u64>;
};
// type = 2
struct peer_data_package_t
{
    u8 type;
    u8 none;
    u16 size;
    u32 package_id;
    u64 data_id;
    u8 data[0];
    using member_list_t = serialization::typelist_t<u8, u8, u16, u32, u64>;
};

#pragma pack(pop)

/// peer tp peer

struct peer_t
{
    rudp_t udp;
    bool ping_ok;
    bool in_request;
    int mark;
    u64 current_request_data_id;
    u64 last_online_timestamp;
    socket_addr_t remote_address;
    peer_t()
        : ping_ok(false)
        , in_request(false)
        , mark(0)
    {
    }
    bool operator==(const peer_t &rt) const { return rt.remote_address == remote_address; }
    bool operator!=(const peer_t &rt) const { return !operator==(rt); }
};

class peer_client_t;
using peer_server_data_recv_t = std::function<void(peer_client_t &, peer_data_package_t &data, peer_t *)>;
using peer_disconnect_t = std::function<void(peer_client_t &, peer_t *)>;
using peer_connect_ok_t = std::function<void(peer_client_t &, peer_t *)>;

/// just request peer server and recv data
/// connect to tracker and get peer nodes, a short connection. connect to tracker when need get get nodes.
/// add to peer_client
class peer_client_t
{
  private:
    session_id_t sid;
    std::vector<std::unique_ptr<peer_t>> peers;
    peer_server_data_recv_t handler;
    peer_disconnect_t disconnect_handler;
    peer_connect_ok_t connect_handler;
    void client_main(peer_t *peer);

  public:
    peer_client_t(session_id_t sid);
    ~peer_client_t();
    peer_client_t(const peer_client_t &) = delete;
    peer_client_t &operator=(const peer_client_t &) = delete;

    peer_t *add_peer(event_context_t &context);
    void connect_to_peer(peer_t *peer, socket_addr_t server_addr);

    peer_client_t &at_peer_data_recv(peer_server_data_recv_t handler);
    peer_client_t &at_peer_disconnect(peer_disconnect_t handler);
    peer_client_t &at_peer_connect(peer_connect_ok_t handler);

    void pull_data_from_peer(u64 data_id);
};

class peer_server_t;

struct speer_t
{
    rudp_t udp;
    bool ping_ok;
    u64 last_online_timestamp;
    socket_addr_t remote_address;
    speer_t()
        : ping_ok(false)
    {
    }
    bool operator==(const speer_t &rt) const { return rt.remote_address == remote_address; }
    bool operator!=(const speer_t &rt) const { return rt.remote_address != remote_address; }
};

using client_join_handler_t = std::function<void(peer_server_t &, speer_t *)>;
using client_data_request_handler_t = std::function<void(peer_server_t &, peer_data_request_t &, speer_t *)>;

struct hash_func_t
{
    u64 operator()(const socket_addr_t &addr) const { return addr.hash(); }
};
/// a peer server can send data to other peer client
/// connect to tracker and register this as a peer node, this is a long connection.

class peer_server_t
{
  private:
    client_join_handler_t client_handler;
    client_data_request_handler_t data_handler;
    std::unordered_map<socket_addr_t, std::unique_ptr<speer_t>, hash_func_t> peers_map;
    void peer_main(speer_t *);

  public:
    peer_server_t() = default;
    peer_server_t(const peer_server_t &) = delete;
    peer_server_t &operator=(const peer_server_t &) = delete;

    speer_t *add_peer(event_context_t &context, socket_addr_t peer_addr);

    peer_server_t &at_client_join(client_join_handler_t handler);
    peer_server_t &at_client_pull(client_data_request_handler_t handler);

    void send_package_to_peer(speer_t *peer, u64 data_id, socket_buffer_t buffer);
};

} // namespace net::p2p