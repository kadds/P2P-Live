/**
* \file endian.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief automatic end-to-end conversion on struct。
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
#include "socket_buffer.hpp"
#include <cassert>
#include <cstring>
#include <tuple>
#include <type_traits>

namespace net::serialization
{
template <typename... Args> struct typelist_t;

/// the type list save members in struct.
template <typename Head> struct typelist_t<Head>
{
    constexpr static bool has_next = false;
    using Type = Head;
};

// Variadic specialization
template <typename Head, typename... Args> struct typelist_t<Head, Args...>
{
    constexpr static bool has_next = true;
    /// next typelist
    using Next = typelist_t<Args...>;
    using Type = Head;
};

} // namespace net::serialization

namespace net::endian
{

///\note GNU extension
///\note using c++20 std::endian when enable c++20
constexpr inline bool little_endian() {
    #ifndef __GNUC__
        return true;
    #else
        return __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__; 
    #endif
}

template <typename T> void cast_struct(T &val);

template <typename T, size_t N> void cast_array(T (&val)[N]);

/// cast struct to network endian (big endian).
/// do nothing at big endian architecture.
template <typename T> inline void cast(T &val)
{
    if constexpr (little_endian())
    {
        if constexpr (std::is_array_v<T>)
        {
            cast_array(val);
        }
        else
        {
            cast_struct(val);
        }
    }
}

/// not cast needed
template <> inline void cast(i8 &val) {}
template <> inline void cast(u8 &val) {}

/// swap bit 0-7 and 8-15
template <> inline void cast(u16 &val)
{
    if constexpr (little_endian())
    {
        auto v = val;
        val = ((v & 0xFF) << 8) | ((v >> 8) & 0xFF);
    }
}

template <> inline void cast(i16 &val) { cast(*(u16 *)&val); }

template <> inline void cast(u32 &val)
{
    if constexpr (little_endian())
    {
        auto v = val;
        val = ((v & 0xFF) << 24) | (((v >> 8) & 0xFF) << 16) | (((v >> 16) & 0xFF) << 8) | ((v >> 24) & 0xFF);
    }
}

template <> inline void cast(i32 &val) { cast(*(u32 *)&val); }

template <> inline void cast(u64 &val)
{
    if constexpr (little_endian())
    {
        auto v = val;
        val = ((v & 0xFF) << 56) | (((v >> 8) & 0xFF) << 48) | (((v >> 16) & 0xFF) << 40) | (((v >> 24) & 0xFF) << 32) |
              (((v >> 32) & 0xFF) << 24) | (((v >> 40) & 0xFF) << 16) | (((v >> 48) & 0xFF) << 8) | ((v >> 56) & 0xFF);
    }
}

template <> inline void cast(i64 &val) { cast(*(i64 *)&val); }

/// specialization of arrays
template <typename T, size_t N> inline void cast_array(T (&val)[N])
{
    for (size_t i = 0; i < N; i++)
    {
        cast(val[i]);
    }
}

template <typename T, typename Typelist> inline void cast_struct_impl(void *val)
{
    cast<typename Typelist::Type>(*(typename Typelist::Type *)val);
    if constexpr (Typelist::has_next)
    {
        cast_struct_impl<T, typename Typelist::Next>(((char *)val) + sizeof(typename Typelist::Type));
    }
}

template <typename T> inline void cast_struct(T &val)
{
    static_assert(std::is_pod_v<T> && !std::is_union_v<T>, "struct should be a POD type.");
    using Typelist = typename T::member_list_t;
    cast<typename Typelist::Type>(*(typename Typelist::Type *)&val);

    if constexpr (Typelist::has_next)
    {
        cast_struct_impl<T, typename Typelist::Next>(((char *)&val) + sizeof(typename Typelist::Type));
    }
}

/// get struct from buffer
///\param buffer the source data
///\param val the target struct to save data after cast
template <typename T> inline bool cast_to(socket_buffer_t &buffer, T &val)
{
    auto ptr_from = buffer.get();
    if (buffer.get_length() < sizeof(val))
        return false;
    memcpy(&val, ptr_from, sizeof(val));
    cast<T>(val);
    return true;
}

/// save struct to buffer
///\param val the source struct
///\param buffer the target buffer to save
///\note the original struct is modified
template <typename T> inline bool save_to(T &val, socket_buffer_t &buffer)
{
    auto ptr = buffer.get();
    if (buffer.get_length() < sizeof(val))
        return false;
    cast<T>(val);
    memcpy(ptr, &val, sizeof(val));
    return true;
}

/// cast struct inplace, buffer and struct addresses must be the same
///\param val the struct to cast
///\param buffer the origin buffer which sames as struct 'val'
template <typename T> inline bool cast_inplace(T &val, socket_buffer_t &buffer)
{
    assert((byte *)&val == buffer.get());
    if (buffer.get_length() < sizeof(val))
        return false;
    cast<T>(val);
    return true;
}
} // namespace net::endian
