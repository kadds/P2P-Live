#pragma once
#include <bit>
#include <cstdint>
#include <tuple>
#include <type_traits>
namespace net
{
using i64 = int64_t;
using u64 = uint64_t;
using i32 = int32_t;
using u32 = uint32_t;
using i16 = int16_t;
using u16 = uint16_t;
using i8 = int8_t;
using u8 = uint8_t;
} // namespace net

namespace net::serialization
{
template <typename... Args> struct typelist_t;

template <typename Head> struct typelist_t<Head>
{
    constexpr static bool has_next = false;
    using Type = Head;
};

// Variadic specialization
template <typename Head, typename... Args> struct typelist_t<Head, Args...>
{
    constexpr static bool has_next = true;
    // constexpr typelist_t<Head, Args...> next() const { return typelist_t<Head, Args...>(); }
    using Next = typelist_t<Args...>;
    using Type = Head;
};

} // namespace net::serialization
namespace net::endian
{

constexpr inline bool little_endian() { return __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__; }

template <typename T> void cast_struct(T &val);

template <typename T, size_t N> void cast_array(T (&val)[N]);

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

template <> inline void cast(i8 &val) {}
template <> inline void cast(u8 &val) {}

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

// template <typename> struct extra_typelist;

// template <template <typename... Args> typename Typelist, typename... Args> struct extra_typelist<Typelist<Args...>>
// {
//     using type = Args;
// };

template <typename T> inline void cast_struct(T &val)
{
    static_assert(std::is_pod_v<T>, "struct must be POD type");
    using Typelist = typename T::member_list_t;
    cast<typename Typelist::Type>(*(typename Typelist::Type *)&val);

    if constexpr (Typelist::has_next)
    {
        cast_struct_impl<T, typename Typelist::Next>(((char *)&val) + sizeof(typename Typelist::Type));
    }
}

} // namespace net::endian
