#include "net/net.hpp"
#include <iostream>
#include <sys/signal.h>

namespace net
{

void init_lib() { signal(SIGPIPE, SIG_IGN); }
void uninit_lib() {}
} // namespace net
