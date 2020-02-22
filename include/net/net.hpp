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

inline constexpr int p2p_context_server_port = 2750;
inline constexpr int p2p_front_server_port = 2751;
inline constexpr int p2p_front_server_context_port = 2752;
inline constexpr int p2p_client_rudp_port = 2780;
inline constexpr int p2p_tracker_tcp_port = 2768;
inline constexpr int p2p_tracker_rudp_port = 2769;

void init_lib();
void uninit_lib();
enum io_result
{
    ok,
    /// io in process
    /// acceptor
    in_process,
    /// continue io request
    cont,
    /// connection is closed by peer/self
    closed,
    /// io request is timeout
    /// connector
    timeout,
    /// io request failed
    failed,
    /// buffer too small
    /// when udp recv
    buffer_too_small,
};
} // namespace net
