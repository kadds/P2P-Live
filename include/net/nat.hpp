#pragma once
#include "endian.hpp"
#include "rudp.hpp"
#include "tcp.hpp"
#include <functional>

namespace net
{

/// TODO: detect NAT type
/// 直接由tracker投递消息，可不经探测NAT类型

enum class nat_type : u8
{
    unknown,
    none,
    full_cone,
    ip_cone,
    port_cone,
    symmetric,
};

#pragma pack(push, 1)

struct nat_request_t
{
    u16 port;
    u16 udp_port;
    u32 ip;
    u32 key;
    using member_list_t = serialization::typelist_t<u16, u16, u32, u32>;
};

struct nat_server_heart_t
{
    u16 port;
    u16 udp_port;
    u32 ip;
    u32 key;
    using member_list_t = serialization::typelist_t<u16, u16, u32, u32>;
};

struct nat_udp_request_t
{
    u32 key;
    using member_list_t = serialization::typelist_t<u32>;
};

struct nat_respond_t
{
    nat_type type;
    u32 key;
    using member_list_t = serialization::typelist_t<u8, u32>;
};

#pragma pack(pop)

constexpr u16 nat_detect_server_port = 6789;

class nat_server_t
{
    tcp::server_t server;
    rudp_t rudp;
    tcp::client_t other_server;
    std::unordered_map<socket_addr_t, nat_request_t> map;

    void client_main(tcp::connection_t conn);
    void other_server_main(tcp::connection_t conn);

  public:
    void bind(event_context_t &ctx, socket_addr_t server_addr, bool reuse_addr = false);
    void connect_second_server(event_context_t &ctx, socket_addr_t server_addr);
};

class net_detector_t
{
    tcp::client_t client;
    rudp_t rudp;

    u32 key;
    bool is_do_request = false;

  public:
    using handler_t = std::function<void(nat_type)>;

    void get_nat_type(event_context_t &ctx, socket_addr_t server, handler_t handler);
};
} // namespace net
