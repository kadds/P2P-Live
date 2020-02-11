#pragma once
#include "endian.hpp"

namespace net::nat
{

/// TODO: nat punching

/**
 * clientA ip:port = ipA:portA
 * clientB ip:port = ipB:portB
 *
 * nat_server: ipS:portS
 *
 * clientA want to connect clientB
 *
 * 1. clientA send connect target to nat_server
 * 2.
 */

#pragma pack(push, 1)

struct nat_check_request_t
{
    u8 version;
    using member_list_t = net::serialization::typelist_t<u8>;
};

struct nat_check_respond_t
{
    u8 version;
    u16 port;
    u32 ip_addr;
    using member_list_t = net::serialization::typelist_t<u8, u16, u32>;
};

struct nat_punch_package_t
{
    u8 version;
    u16 port;
    u32 ip_addr;
};

#pragma pack(pop)

class nat_punch_server_t
{
  public:
    void bind();
};

} // namespace net::nat
