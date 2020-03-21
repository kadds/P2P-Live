/**
 * \file msg.hpp
 * \author kadds (itmyxyf@gmail.com)
 * \brief all p2p message types
 * \version 0.1
 * \date 2020-03-21
 *
 * @copyright Copyright (c) 2020.
 * This file is part of P2P-Live.
 *
 * P2P-Live is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * P2P-Live is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with P2P-Live. If not, see <http: //www.gnu.org/licenses/>.
 *
 */

#pragma once
#include "../endian.hpp"
#include "../net.hpp"
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

// ---------------- peer request/respond begin ----------------
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
// ----------------------- peer request/respond end ---------------------

// ------------------------ tracker request/respond begin ---------------

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
// ----------------------------------- tracker request/respond end -----------------------

} // namespace net::p2p