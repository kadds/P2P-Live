#include "net/net.hpp"
#include <iostream>
#include <sys/signal.h>

namespace net
{
void sigpipe(int p) {}
void sigint(int p)
{
    std::cout << "signal INT. exit ...";
    exit(0);
}

void init_lib()
{
    signal(SIGPIPE, sigpipe);
    signal(SIGINT, sigint);
}
} // namespace net
