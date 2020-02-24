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
    LOG(ERROR) << "connect to tracker server failed. server: " << remote.to_string()
               << ", reason: " << net::connection_state_strings[(int)state] << ".";
    LOG(INFO) << "sleep 5 second to reconnect server.";
    net::event_loop_t::current().add_timer(net::make_timer(net::make_timespan(5), [&client]() {
        client.connect_server(*app_context, net::socket_addr_t(FLAGS_tip, FLAGS_tport), FLAGS_timeout * 1000);
    }));
}

void server_connect(net::p2p::tracker_node_client_t &client, net::socket_addr_t remote)
{
    LOG(ERROR) << "connect to tracker server ok. server: " << remote.to_string();
}

void node_request_connect(net::p2p::tracker_node_client_t &client, net::p2p::peer_node_t node)
{
    net::socket_addr_t remote(node.ip, node.port);

    LOG(INFO) << "new node request connect " << remote.to_string() << " udp:" << node.udp_port << ".";
}

void on_peer_connect(net::p2p::peer_t &ps, net::p2p::peer_info_t *peer)
{
    net::socket_addr_t remote = peer->remote_address;
    LOG(INFO) << "new peer connect " << remote.to_string();
}

void on_peer_disconnect(net::p2p::peer_t &ps, net::p2p::peer_info_t *peer)
{
    net::socket_addr_t remote = peer->remote_address;
    LOG(INFO) << "peer disconnect " << remote.to_string();
}

struct session_fragment_content
{
    std::queue<size_t> queue;
    std::unordered_map<net::u64, size_t> map;

    std::vector<net::socket_buffer_t> vector;

    void add_data() {}

    void remove_data() {}
};

struct session_meta_content
{
    std::unordered_map<net::u64, net::socket_buffer_t> raw_data;
    void add_data(net::socket_buffer_t buffer, net::u64 key) { raw_data[key] = buffer; }
};

std::unordered_map<net::u64, std::unique_ptr<session_fragment_content>> fragment_data;
std::unordered_map<net::u64, std::unique_ptr<session_meta_content>> meta_data;

void on_fragment_pull_request(net::p2p::peer_t &ps, net::p2p::peer_info_t *p, net::u64 fid) {}

void on_meta_pull_request(net::p2p::peer_t &ps, net::p2p::peer_info_t *p, net::u64 key) {}

void on_fragment_recv(net::p2p::peer_t &ps, net::p2p::peer_info_t *p, net::socket_buffer_t buffer, net::u64 fid) {}

void on_meta_data_recv(net::p2p::peer_t &ps, net::p2p::peer_info_t *p, net::socket_buffer_t buffer, net::u64 key) {}

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
    tracker_client->on_tracker_server_connect(server_connect);

    std::unique_ptr<net::p2p::peer_t> peer = std::make_unique<net::p2p::peer_t>(0);
    peer->bind(context);
    peer->accept_channels({1, 2});
    peer->on_peer_connect(on_peer_connect);
    peer->on_peer_disconnect(on_peer_disconnect);
    peer->on_fragment_pull_request(on_fragment_pull_request);
    peer->on_fragment_recv(on_fragment_recv);
    peer->on_meta_pull_request(on_meta_pull_request);
    peer->on_meta_data_recv(on_meta_data_recv);

    LOG(INFO) << "run event loop";
    auto ret = app_context->run();
    net::uninit_lib();
    return ret;
}
