#include "net/event.hpp"
#include "net/net.hpp"
#include "net/socket.hpp"
#include "net/tcp.hpp"
#include "net/udp.hpp"
#include <glog/logging.h>
#include <iostream>
#include <thread>

net::event_context_t *app_context;

void thread_main()
{
    net::event_context_t context(net::event_strategy::epoll);
    net::event_loop_t looper;
    context.add_event_loop(&looper);

    looper.run();
}

void atexit_func() { google::ShutdownGoogleLogging(); }

int main(int argc, char **argv)
{
    google::InitGoogleLogging(argv[0]);
    google::SetLogDestination(google::GLOG_FATAL, "./server.log");
    google::SetLogDestination(google::GLOG_ERROR, "./server.log");
    google::SetLogDestination(google::GLOG_INFO, "./server.log");
    google::SetLogDestination(google::GLOG_WARNING, "./server.log");
    google::SetStderrLogging(google::GLOG_INFO);

    atexit(atexit_func);

    net::init_lib();
    LOG(INFO) << "init libnet";
    net::event_context_t context(net::event_strategy::epoll);
    app_context = &context;
    LOG(INFO) << "create application event context";

    net::event_loop_t looper;
    app_context->add_event_loop(&looper);

    std::thread thd(thread_main);
    thd.detach();

    LOG(INFO) << "run event loop";
    return looper.run();
}
