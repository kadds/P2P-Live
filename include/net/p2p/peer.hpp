#pragma once
#include "../endian.hpp"
#include "../net.hpp"
#include "../rudp.hpp"
#include "../socket_addr.hpp"
#include "../tcp.hpp"
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// forward
namespace net
{
class socket_t;
}

namespace net::p2p
{
using fragment_id_t = u64;
using session_id_t = u32;
namespace peer_msg_type
{
enum : u8
{
    init_request = 0,
    init_respond,
    fragment_request,
    fragment_respond,

    meta_respond = 9,

    cancel = 10,
    get_meta = 11,
    key_exchange = 12,

    heart = 0xFF,
};
}

#pragma pack(push, 1)

struct peer_init_request_t
{
    u8 type;
    session_id_t sid;
    using member_list_t = serialization::typelist_t<u8, session_id_t>;
};

struct peer_fragment_request_t
{
    u8 type;
    u8 priority;
    u8 count;
    fragment_id_t ids[0];
    using member_list_t = serialization::typelist_t<u8, u8, u8>;
};

struct peer_cancel_t
{
    u8 type;
    u8 count;
    fragment_id_t id[0];
    using member_list_t = serialization::typelist_t<u8, u8>;
};

struct peer_request_metainfo_t
{
    u8 type;
    u64 key;
    using member_list_t = serialization::typelist_t<u8, u64>;
};

struct peer_init_respond_t
{
    u8 type;
    fragment_id_t first_data_id;
    fragment_id_t last_data_id;
    using member_list_t = serialization::typelist_t<u8, fragment_id_t, fragment_id_t>;
};

struct peer_fragment_respond_t
{
    u8 type;
    fragment_id_t fid;
    u32 frame_size;
    u8 data[0];
    using member_list_t = serialization::typelist_t<u8, u16, fragment_id_t, u32>;
};

struct peer_fragment_respond_rest_t
{
    u8 type;
    u8 data[0];
    using member_list_t = serialization::typelist_t<u8>;
};

struct peer_meta_respond_t
{
    u8 type;
    u64 key;
    u8 data[0];
    using member_list_t = serialization::typelist_t<u8, u64>;
};

#pragma pack(pop)

/// peer tp peer

struct peer_info_t
{
    std::queue<std::pair<std::vector<fragment_id_t>, u8>> frag_request_queue;
    std::queue<u64> meta_request_queue;
    std::queue<std::tuple<fragment_id_t, socket_buffer_t>> fragment_send_queue;
    std::queue<std::tuple<u64, socket_buffer_t>> meta_send_queue;

    std::unordered_map<fragment_id_t, socket_buffer_t> send_buffers;

    std::unordered_map<fragment_id_t, socket_buffer_t> recv_buffers;

    socket_addr_t remote_address;
    microsecond_t last_ping;
    bool has_connect;
    peer_info_t()
        : last_ping(0)
        , has_connect(false)
    {
    }

    bool operator==(const peer_info_t &rt) const { return rt.remote_address == remote_address; }
    bool operator!=(const peer_info_t &rt) const { return !operator==(rt); }
};

struct peer_hash_t
{
    u64 operator()(const socket_addr_t &p) const { return p.hash(); }
};

class peer_t
{
  public:
    using peer_data_recv_t = std::function<void(peer_t &, socket_buffer_t &, u64 id_key, peer_info_t *)>;
    using peer_disconnect_t = std::function<void(peer_t &, peer_info_t *)>;
    using peer_connect_ok_t = std::function<void(peer_t &, peer_info_t *)>;

    using pull_request_t = std::function<void(peer_t &, peer_info_t *, u64 id_key)>;

  private:
    rudp_t udp;
    session_id_t sid;
    std::unordered_map<socket_addr_t, std::unique_ptr<peer_info_t>, peer_hash_t> peers;
    std::vector<std::unique_ptr<peer_info_t>> noconnect_peers;
    peer_data_recv_t meta_recv_handler;
    peer_data_recv_t fragment_recv_handler;

    peer_disconnect_t disconnect_handler;
    peer_connect_ok_t connect_handler;
    pull_request_t fragment_handler;
    pull_request_t meta_handler;
    void main();

    std::queue<peer_info_t *> sendable_peers;

    void update_fragments(std::vector<fragment_id_t> ids, u8 priority, peer_info_t *target);
    void update_metainfo(u64 key, peer_info_t *target);

    void send_init(peer_info_t *target);
    void send_fragments(fragment_id_t id, socket_buffer_t buffer, peer_info_t *target);
    void do_write();

    peer_info_t *find_peer(socket_addr_t addr);

  public:
    peer_t(session_id_t sid);
    ~peer_t();
    peer_t(const peer_t &) = delete;
    peer_t &operator=(const peer_t &) = delete;

    void bind(event_context_t &context);
    void bind(event_context_t &context, socket_addr_t addr_to_bind, bool reuse_addr = false);

    peer_info_t *add_peer();
    void connect_to_peer(peer_info_t *peer, socket_addr_t server_addr);
    void disconnect(peer_info_t *peer);

    peer_t &on_meta_data_recv(peer_data_recv_t handler);
    peer_t &on_fragment_recv(peer_data_recv_t handler);
    peer_t &on_peer_disconnect(peer_disconnect_t handler);
    peer_t &on_peer_connect(peer_connect_ok_t handler);

    peer_t &on_fragment_pull_request(pull_request_t handler);
    peer_t &on_meta_pull_request(pull_request_t handler);

    void pull_fragment_from_peer(peer_info_t *peer, std::vector<fragment_id_t> fid, u8 priority);
    void pull_meta_data(peer_info_t *peer, u64 key);

    void send_fragment_to_peer(peer_info_t *peer, fragment_id_t fid, socket_buffer_t buffer);
    void send_meta_data_to_peer(peer_info_t *peer, u64 key, socket_buffer_t buffer);

    socket_t *get_socket() const { return udp.get_socket(); }
};

} // namespace net::p2p