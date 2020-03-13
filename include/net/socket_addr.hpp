/**
* \file socket_addr.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief Socket address c++ wrapper
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
#include "net.hpp"
#include <string>
namespace net
{
class socket_addr_t
{
    sockaddr_in so_addr;

  public:
    socket_addr_t();
    /// init address (any:port)
    socket_addr_t(int port);
    socket_addr_t(u32 addr, int port);
    socket_addr_t(sockaddr_in addr);
    socket_addr_t(std::string addr, int port);
    std::string to_string() const;
    std::string get_addr() const;
    u32 v4_addr() const;
    int get_port() const;

    sockaddr_in get_raw_addr() { return so_addr; }

    bool operator==(const socket_addr_t &addr) const
    {
        return addr.so_addr.sin_port == so_addr.sin_port && addr.so_addr.sin_family == so_addr.sin_family &&
               addr.so_addr.sin_addr.s_addr == so_addr.sin_addr.s_addr;
    }

    bool operator!=(const socket_addr_t &addr) const { return !operator==(addr); }
    u64 hash() const { return ((u64)so_addr.sin_addr.s_addr << 32) | so_addr.sin_port; };
};
}; // namespace net