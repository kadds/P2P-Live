#pragma once
#ifndef OS_WINDOWS
#include <arpa/inet.h>
#include <cstddef>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#else
#endif
#include "net_exception.hpp"

typedef unsigned char byte;

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

inline constexpr int p2p_port = 1238;
inline constexpr int command_port = 1237;

void init_lib();
enum io_result
{
    ok,
    in_process,
    cont,
    closed,
    timeout,
    failed,
    buffer_too_small,
};
} // namespace net
