/**
* \file net_exception.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief exceptions declaration
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
#include <exception>
#include <string>
namespace net
{
enum class connection_state
{
    closed,
    close_by_peer,
    connection_refuse,
    address_in_used,
    no_resource,
    timeout,
    secure_check_failed,
    invalid_request,
};

static const char *connection_state_strings[] = {"the connection is closed unexpectedly",
                                                 "the connection is closed by peer",
                                                 "remote connection refused",
                                                 "local address is occupied",
                                                 "system resource limit, check system memory and file desces",
                                                 "connection timeout",
                                                 "security check failed",
                                                 "invalid data request"};

class net_connect_exception : public std::exception
{
    std::string str;
    connection_state state;

  public:
    net_connect_exception(std::string str, connection_state state)
        : str(str)
        , state(state)
    {
    }

    const char *what() { return str.c_str(); }

    connection_state get_state() const { return state; }
};

class net_param_exception : public std::exception
{
    std::string str;

  public:
    net_param_exception(std::string str)
        : str(str)
    {
    }

    const char *what() { return str.c_str(); }
};

class net_io_exception : public std::exception
{
    std::string str;

  public:
    net_io_exception(std::string str)
        : str(str)
    {
    }

    const char *what() { return str.c_str(); }
};

} // namespace net