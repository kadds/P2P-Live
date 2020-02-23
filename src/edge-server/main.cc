#include "net/event.hpp"
#include "net/p2p/peer.hpp"
#include "net/p2p/tracker.hpp"
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <thread>

DEFINE_string(ip, "0.0.0.0", "edge server ip address");
DEFINE_uint32(port, 2769, "edge server bind port");
DEFINE_bool(resuse, false, "reuse address");
DEFINE_uint32(threads, 0, "thread count");
DEFINE_bool(cmd, false, "command mode");
DEFINE_string(tip, "0.0.0.0", "tracker server address");
DEFINE_uint32(tport, 2769, "tracker server port");
DEFINE_uint32(timeout, 5000, "tracker server connect timeout (ms)");

net::event_context_t *app_context;

void thread_main()
{
    net::event_loop_t looper;
    app_context->add_event_loop(&looper);
    looper.run();
}

static void atexit_func() { google::ShutdownGoogleLogging(); }

int main(int argc, char **argv)
{
    google::InitGoogleLogging(argv[0]);
    google::ParseCommandLineFlags(&argc, &argv, true);
    google::SetLogDestination(google::GLOG_FATAL, "./edge_server.log");
    google::SetLogDestination(google::GLOG_ERROR, "./edge_server.log");
    google::SetLogDestination(google::GLOG_INFO, "./edge_server.log");
    google::SetLogDestination(google::GLOG_WARNING, "./edge_server.log");
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

    for (int i = 0; i < FLAGS_threads - 1; i++)
    {
        std::thread thd(thread_main);
        thd.detach();
    }

    std::unique_ptr<net::p2p::tracker_node_client_t> tracker_client =
        std::make_unique<net::p2p::tracker_node_client_t>();
    tracker_client->connect_server(context, net::socket_addr_t(FLAGS_tip, FLAGS_tport), FLAGS_timeout / 1000);

    LOG(INFO) << "run event loop";
    auto ret = looper.run();
    net::uninit_lib();
    return ret;
}
