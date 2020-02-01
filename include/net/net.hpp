#pragma once
#ifndef OS_WINDOWS
#include <arpa/inet.h>
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
