/**
* \file socket_buffer.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief socket buffer is a buffer container with move and reference count semantics
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
#include "net/net.hpp"
#include <atomic>
#include <google/protobuf/message.h>
namespace net
{

/// The buffer container can be initialized by size, or using existing memory. It is managed by the
/// socket_buffer_t in the first case, and is managed by the user in the second case.
class socket_buffer_t
{
  public:
    /// a control block used by socket buffer
    struct socket_buffer_header_t
    {
        /// shared reference count
        std::atomic_int ref_count;
    };

  private:
    byte *ptr;
    socket_buffer_header_t *header;
    u64 buffer_size;
    u64 valid_data_length;
    u64 walk_offset;

  private:
    static socket_buffer_t from_struct_inner(byte *buffer_ptr, u64 buffer_length);

  public:
    struct except_buffer_helper_t
    {
        socket_buffer_t *buf;
        explicit except_buffer_helper_t(socket_buffer_t *buf)
            : buf(buf)
        {
        }

        except_buffer_helper_t length(u64 len);
        except_buffer_helper_t origin_length();

        socket_buffer_t &operator()() const { return *buf; }
    };
    /// init buffer with nothing, the pointer will not be initialized.
    socket_buffer_t();
    socket_buffer_t(u64 len);
    socket_buffer_t(const google::protobuf::Message &msg)
        : socket_buffer_t(msg.ByteSizeLong())
    {
        msg.SerializeWithCachedSizesToArray(ptr);
        valid_data_length = buffer_size;
    }

    /// init buffer with pointer
    socket_buffer_t(byte *buffer_ptr, u64 buffer_length);

    socket_buffer_t(const socket_buffer_t &);
    socket_buffer_t &operator=(const socket_buffer_t &);

    // move operation
    socket_buffer_t(socket_buffer_t &&buf);
    socket_buffer_t &operator()(socket_buffer_t &&buf);

    ~socket_buffer_t();

    template <typename T> static socket_buffer_t from_struct(T &buf)
    {
        static_assert(std::is_pod_v<T>);
        return from_struct_inner((byte *)&buf, sizeof(T));
    }

    static socket_buffer_t from_string(std::string str);

    byte *get_base_ptr() const { return ptr; }

    /// get pointer at current offset
    byte *get() const { return ptr + walk_offset; }

    /// get origin data length
    u64 get_data_length() const { return valid_data_length; }

    /// get origin buffer length
    u64 get_buffer_origin_length() const { return buffer_size; }

    /// get data length start at the current offset
    u64 get_length() const { return valid_data_length - walk_offset; }

    u64 get_walk_offset() const { return walk_offset; }

    /// reset offset and set data length to offset
    void finish_walk()
    {
        valid_data_length = walk_offset;
        walk_offset = 0;
    }

    /// except size to read/write
    ///\note call it before read/write socket data. Decide on the size of the data sent and received
    except_buffer_helper_t expect() { return except_buffer_helper_t(this); }

    long write_string(const std::string &str);
    std::string to_string() const;

    /// walk in buffer
    void walk_step(u64 delta)
    {
        walk_offset += delta;
        if (walk_offset > valid_data_length)
            walk_offset = valid_data_length;
    }

    /// memzero to buffer
    void clear();
};

}; // namespace net