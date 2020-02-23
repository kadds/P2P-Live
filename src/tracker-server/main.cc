#include "net/event.hpp"
#include "net/p2p/tracker.hpp"
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <thread>

DEFINE_string(ip, "0.0.0.0", "tracker server bind ip address");
DEFINE_uint32(port, 2769, "tracker server port");
DEFINE_uint32(rudp_port, 2770, "tracker server rudp port");
DEFINE_uint32(threads, 0, "threads count");
DEFINE_bool(reuse, false, "resuse address");

net::event_context_t *app_context;

void thread_main()
{
    net::event_loop_t looper;
    app_context->add_event_loop(&looper);
    looper.run();
}

static void atexit_func()
{
    LOG(INFO) << "exit server...";
    google::ShutdownGoogleLogging();
}

int main(int argc, char **argv)
{
    google::InitGoogleLogging(argv[0]);
    google::ParseCommandLineFlags(&argc, &argv, true);
    google::SetLogDestination(google::GLOG_FATAL, "./tracker_server.log");
    google::SetLogDestination(google::GLOG_ERROR, "./tracker_server.log");
    google::SetLogDestination(google::GLOG_INFO, "./tracker_server.log");
    google::SetLogDestination(google::GLOG_WARNING, "./tracker_server.log");
    google::SetStderrLogging(google::GLOG_INFO);

    atexit(atexit_func);

    LOG(INFO) << "init libnet";
    net::init_lib();

    LOG(INFO) << "create application context";
    net::event_context_t context(net::event_strategy::epoll);
    app_context = &context;

    net::event_loop_t looper;
    app_context->add_event_loop(&looper);

    if (FLAGS_threads == 0)
    {
        FLAGS_threads = 4;
    }
    LOG(INFO) << "thread detect " << FLAGS_threads;

    for (int i = 0; i < FLAGS_threads; i++)
    {
        std::thread thd(thread_main);
        thd.detach();
    }

    LOG(INFO) << "start tracker server at " << FLAGS_ip << ":" << FLAGS_port;

    std::unique_ptr<net::p2p::tracker_server_t> tracker_server = std::make_unique<net::p2p::tracker_server_t>();
    tracker_server->bind(context, net::socket_addr_t(FLAGS_ip, FLAGS_port), FLAGS_reuse);

    LOG(INFO) << "run event loop";
    auto ret = looper.run();
    net::uninit_lib();
    return ret;
}