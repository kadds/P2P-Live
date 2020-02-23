#include "net/event.hpp"
#include "net/p2p/peer.hpp"
#include "net/p2p/tracker.hpp"
#include "net/socket.hpp"
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

void thread_main() { app_context->run(); }

static void atexit_func() { google::ShutdownGoogleLogging(); }

void server_connect_error(net::p2p::tracker_node_client_t &client, net::socket_addr_t remote,
                          net::connection_state state)
{
    LOG(ERROR) << "connect to tracker failed. server: " << remote.to_string()
               << ", reason: " << net::connection_state_strings[(int)state] << ".";
}

void node_request_connect(net::p2p::tracker_node_client_t &client, net::p2p::peer_node_t node)
{
    net::socket_addr_t remote(node.ip, node.port);

    LOG(INFO) << "new node request connect " << remote.to_string() << " udp:" << node.udp_port << ".";
}

int main(int argc, char **argv)
{
    google::InitGoogleLogging(argv[0]);
    google::ParseCommandLineFlags(&argc, &argv, false);
    google::SetLogDestination(google::GLOG_FATAL, "./edge-server.log");
    google::SetLogDestination(google::GLOG_ERROR, "./edge-server.log");
    google::SetLogDestination(google::GLOG_INFO, "./edge-server.log");
    google::SetLogDestination(google::GLOG_WARNING, "./edge-server.log");
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

    std::unique_ptr<net::p2p::tracker_node_client_t> tracker_client =
        std::make_unique<net::p2p::tracker_node_client_t>();
    tracker_client->config(true, 0, "edge server key");
    tracker_client->connect_server(context, net::socket_addr_t(FLAGS_tip, FLAGS_tport), FLAGS_timeout * 1000);
    tracker_client->on_error(server_connect_error);
    tracker_client->on_node_request_connect(node_request_connect);

    LOG(INFO) << "run event loop";
    auto ret = app_context->run();
    net::uninit_lib();
    return ret;
}
