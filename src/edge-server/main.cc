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

void node_request_connect(net::p2p::tracker_node_client_t &client, net::p2p::peer_node_t node, net::p2p::peer_t *peer)
{
    net::socket_addr_t remote(node.ip, node.port);

    LOG(INFO) << "new node request connect " << remote.to_string() << " udp:" << node.udp_port << ".";

    client.request_connect_node(node, peer->get_udp());
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

    void add_data(net::socket_buffer_t buffer, net::u64 fragment_id)
    {
        queue.emplace(vector.size());
        map.emplace(fragment_id, vector.size());
        vector.emplace_back(buffer);

        while (queue.size() > 50)
        {
            queue.pop();
        }
    }

    std::optional<net::socket_buffer_t> get_data(net::u64 key)
    {
        auto idx = map.find(key);
        if (idx != map.end())
        {
            return vector[idx->second];
        }
        return std::make_optional<net::socket_buffer_t>();
    }
};

struct session_meta_content
{
    std::unordered_map<net::u64, net::socket_buffer_t> raw_data;
    void add_data(net::socket_buffer_t buffer, net::u64 key) { raw_data[key] = buffer; }
    std::optional<net::socket_buffer_t> get_data(net::u64 key)
    {
        auto idx = raw_data.find(key);
        if (idx != raw_data.end())
        {
            return idx->second;
        }
        return std::make_optional<net::socket_buffer_t>();
    }
};

struct channel_t
{
    std::unique_ptr<session_fragment_content> fragment;
    std::unique_ptr<session_meta_content> meta;
};

std::unordered_map<net::u64, std::unordered_map<int, channel_t>> globl_data;

/// BUG: add mutex here!!!
void on_fragment_pull_request(net::p2p::peer_t &ps, net::p2p::peer_info_t *p, net::u64 fid, int channel)
{
    auto buf = globl_data[p->sid][channel].fragment->get_data(fid);
    if (buf.has_value())
    {
        ps.send_meta_data_to_peer(p, fid, channel, buf.value());
    }
    else
    {
        LOG(INFO) << "request from " << p->remote_address.to_string() << "'s fragment " << fid << " channel " << channel
                  << " is unknown.";
    }
}

void on_meta_pull_request(net::p2p::peer_t &ps, net::p2p::peer_info_t *p, net::u64 key, int channel)
{
    auto buf = globl_data[p->sid][channel].meta->get_data(key);
    if (buf.has_value())
    {
        ps.send_meta_data_to_peer(p, key, channel, buf.value());
    }
    else
    {
        LOG(INFO) << "request from " << p->remote_address.to_string() << "'s meta info " << key << " channel "
                  << channel << " is unknown.";
    }
}

void on_fragment_recv(net::p2p::peer_t &ps, net::p2p::peer_info_t *p, net::socket_buffer_t buffer, net::u64 fid,
                      int channel)
{
    globl_data[p->sid][channel].fragment->add_data(buffer, fid);
}

void on_meta_data_recv(net::p2p::peer_t &ps, net::p2p::peer_info_t *p, net::socket_buffer_t buffer, net::u64 key,
                       int channel)
{
    globl_data[p->sid][channel].meta->add_data(buffer, key);
}

int main(int argc, char **argv)
{
    google::InitGoogleLogging(argv[0]);
    google::ParseCommandLineFlags(&argc, &argv, false);
    google::SetLogDestination(google::GLOG_FATAL, "./edge-server.fatal.log");
    google::SetLogDestination(google::GLOG_ERROR, "./edge-server.error.log");
    google::SetLogDestination(google::GLOG_INFO, "./edge-server.info.log");
    google::SetLogDestination(google::GLOG_WARNING, "./edge-server.warning.log");
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
    std::unique_ptr<net::p2p::peer_t> peer = std::make_unique<net::p2p::peer_t>(0);

    tracker_client->config(true, 0, "edge server key");
    tracker_client->connect_server(context, net::socket_addr_t(FLAGS_tip, FLAGS_tport), FLAGS_timeout * 1000);
    tracker_client->on_error(server_connect_error);
    tracker_client->on_node_request_connect(
        std::bind(node_request_connect, std::placeholders::_1, std::placeholders::_2, peer.get()));
    tracker_client->on_tracker_server_connect(server_connect);

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
