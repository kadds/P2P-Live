#include "net/net.hpp"
#include <iostream>

namespace net
{
#ifdef OS_WINDOWS
WSAData wsa;
#endif
void init_lib()
{
#ifndef OS_WINDOWS
    signal(SIGPIPE, SIG_IGN);
#else
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}
void uninit_lib()
{
#ifdef OS_WINDOWS
    WSACleanup();
#endif
}
} // namespace net
