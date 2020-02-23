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

void thread_main() { app_context->run(); }

static void atexit_func()
{
    LOG(INFO) << "exit server...";
    google::ShutdownGoogleLogging();
}

void on_shared_peer_add(net::p2p::tracker_server_t &server, net::p2p::peer_node_t node, net::u64 sid)
{
    net::socket_addr_t addr(node.ip, node.port);
    LOG(INFO) << "new shared peer " << addr.to_string() << " udp: " << node.port << " sid: " << sid;
}

void on_shared_peer_remove(net::p2p::tracker_server_t &server, net::p2p::peer_node_t node, net::u64 sid)
{
    net::socket_addr_t addr(node.ip, node.port);
    LOG(INFO) << "exit shared peer " << addr.to_string() << " udp: " << node.port << " sid " << sid;
}
void on_shared_peer_error(net::p2p::tracker_server_t &server, net::socket_addr_t addr, net::u64 sid,
                          net::connection_state state)

{
    LOG(INFO) << "exit shared peer " << addr.to_string() << " sid: " << sid
              << " reason: " << net::connection_state_strings[(int)state];
}

int main(int argc, char **argv)
{
    google::InitGoogleLogging(argv[0]);
    google::ParseCommandLineFlags(&argc, &argv, false);
    google::SetLogDestination(google::GLOG_FATAL, "./tracker-server.log");
    google::SetLogDestination(google::GLOG_ERROR, "./tracker-server.log");
    google::SetLogDestination(google::GLOG_INFO, "./tracker-server.log");
    google::SetLogDestination(google::GLOG_WARNING, "./tracker-server.log");
    google::SetStderrLogging(google::GLOG_INFO);

    atexit(atexit_func);

    LOG(INFO) << "init libnet";
    net::init_lib();

    LOG(INFO) << "create application context";
    net::event_context_t context(net::event_strategy::epoll);
    app_context = &context;

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

    LOG(INFO) << "start tracker server at " << FLAGS_ip << ":" << FLAGS_port;

    std::unique_ptr<net::p2p::tracker_server_t> tracker_server = std::make_unique<net::p2p::tracker_server_t>();
    tracker_server->bind(context, net::socket_addr_t(FLAGS_ip, FLAGS_port), FLAGS_reuse);
    /// configurate edge server key
    tracker_server->config("edge server key");
    tracker_server->on_shared_peer_add_connection(on_shared_peer_add);
    tracker_server->on_shared_peer_remove_connection(on_shared_peer_remove);
    tracker_server->on_shared_peer_error(on_shared_peer_error);

    LOG(INFO) << "run event loop";
    auto ret = app_context->run();
    net::uninit_lib();
    return ret;
}