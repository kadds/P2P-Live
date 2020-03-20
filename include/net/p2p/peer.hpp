/**
* \file peer.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief peer server/client
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
#include "../socket_addr.hpp"
#include "../tcp.hpp"
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/**
 * 如果AB都是公网用户
 *      直接连接 (多出现在边缘服务节点传输数据，延迟最低)
 *
 * 如果公网用户A 和NAT锥形用户B
 * 如果 B->A (多出现在1级分发时，客户端从边缘服务节点拉取数据)
 *      B主动连接A端口
 * 如果A->B （反向连接）（少见）
 *      A->tracker->B->A
 *      A连接到tracker发送请求
 *      tracker 把request消息转发给B
 *      B直接连接上A的外网IP和端口
 *
 * 锥形用户A to 锥形用户B
 * A->B
 *      A->tracker->B->tracker->A | A<=>B
 *      A通过 udp 连接到 tracker，同时在NAT上留下一个洞
 *      Tracker把A 的 request消息转发给B， 其中包含A打通的NAT端口
 *      B得知A的公网地址，尝试连接A （如果为A全锥形型NAT，成功）
 *      B通过udp连接tracker，同时在NAT上留下一个洞
 *      Tracker把B的NAT打通的端口发送到A
 *      A和B相互同时连接，最少两次发包成功
 *
 * 对称型A to 锥形用户B
 *      X
 * 对称A to 对称B
 *      X
 *
 */

namespace net
{
class socket_t;
}

namespace net::p2p
{
using fragment_id_t = u64;
using session_id_t = u32;
using channel_t = u8;

namespace peer_msg_type
{
enum : u8
{
    init_request = 0,
    init_respond,
    fragment_request,
    fragment_respond,
    fragment_respond_rest,

    meta_respond = 9,

    cancel = 10,
    get_meta = 11,
    key_exchange = 12,

    heart = 0xFF,
};
}
// ---------------- request/respond begin ----------------
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
    using member_list_t = serialization::typelist_t<u8, fragment_id_t, u32>;
};

struct peer_fragment_rest_respond_t
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
// ----------------------- request/respond end ---------------------

struct channel_info_t
{
    std::queue<std::tuple<std::vector<fragment_id_t>, u8>> frag_request_queue;
    std::queue<u64> meta_request_queue;

    std::queue<std::tuple<fragment_id_t, socket_buffer_t>> fragment_send_queue;
    std::queue<std::tuple<u64, socket_buffer_t>> meta_send_queue;

    fragment_id_t fragment_recv_id;
    socket_buffer_t fragment_recv_buffer_cache;

    rudp_connection_t conn;
};

struct peer_info_t
{
    std::unordered_map<channel_t, channel_info_t> channel; // map channel -> channel info
    /// udp port address
    socket_addr_t remote_address;
    u64 sid;
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

/// hash function
struct peer_hash_t
{
    u64 operator()(const socket_addr_t &p) const { return p.hash(); }
};

class peer_t
{
  public:
    using peer_data_recv_t = std::function<void(peer_t &, peer_info_t *, socket_buffer_t, u64 id_key, int channel)>;
    using peer_disconnect_t = std::function<void(peer_t &, peer_info_t *)>;
    using peer_connect_ok_t = std::function<void(peer_t &, peer_info_t *)>;

    using pull_request_t = std::function<void(peer_t &, peer_info_t *, u64 id_key, int channel)>;

  private:
    /// Data socket to transfer data
    rudp_t udp;
    /// session id request/provide
    session_id_t sid;
    /// peer map
    std::unordered_map<socket_addr_t, std::unique_ptr<peer_info_t>, peer_hash_t> peers;
    std::vector<std::unique_ptr<peer_info_t>> noconnect_peers;

    peer_data_recv_t meta_recv_handler;
    peer_data_recv_t fragment_recv_handler;

    peer_disconnect_t disconnect_handler;
    peer_connect_ok_t connect_handler;
    pull_request_t fragment_handler;
    pull_request_t meta_handler;

    u64 heartbeat_tick = 30000000;
    u64 disconnect_tick = 120000000;
    std::vector<channel_t> channels;

  private:
    void main(rudp_connection_t conn);
    void heartbeat(rudp_connection_t conn);

    void update_fragments(std::vector<fragment_id_t> ids, u8 priority, rudp_connection_t conn);
    void update_metainfo(u64 key, rudp_connection_t conn);
    void send_metainfo(u64 key, socket_buffer_t buffer, rudp_connection_t conn);
    void send_fragments(fragment_id_t id, socket_buffer_t buffer, rudp_connection_t conn);

    void send_init(rudp_connection_t conn);
    void async_do_write(peer_info_t *peer, int channel);

    peer_info_t *find_peer(socket_addr_t addr);

    void bind_udp();

  public:
    peer_t(session_id_t sid);
    ~peer_t();
    peer_t(const peer_t &) = delete;
    peer_t &operator=(const peer_t &) = delete;

    void bind(event_context_t &context);
    void bind(event_context_t &context, socket_addr_t addr_to_bind, bool reuse_addr = false);

    void accept_channels(const std::vector<channel_t> &channels);

    peer_info_t *add_peer();
    void connect_to_peer(peer_info_t *peer, socket_addr_t remote_peer_udp_addr);
    void disconnect(peer_info_t *peer);

    bool has_connect_peer(socket_addr_t remote_peer_udp_addr);

    peer_t &on_meta_data_recv(peer_data_recv_t handler);
    peer_t &on_fragment_recv(peer_data_recv_t handler);
    peer_t &on_peer_disconnect(peer_disconnect_t handler);
    peer_t &on_peer_connect(peer_connect_ok_t handler);

    peer_t &on_fragment_pull_request(pull_request_t handler);
    peer_t &on_meta_pull_request(pull_request_t handler);

    void pull_fragment_from_peer(peer_info_t *peer, std::vector<fragment_id_t> fid, channel_t channel, u8 priority);
    void pull_meta_data(peer_info_t *peer, u64 key, channel_t channel);

    void send_fragment_to_peer(peer_info_t *peer, fragment_id_t fid, channel_t channel, socket_buffer_t buffer);
    void send_meta_data_to_peer(peer_info_t *peer, u64 key, channel_t channel, socket_buffer_t buffer);

    socket_t *get_socket() const { return udp.get_socket(); }

    rudp_t &get_udp() { return udp; }
};

} // namespace net::p2p