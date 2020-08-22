#include "peer.hpp"
#include "net/event.hpp"
#include "net/p2p/peer.hpp"
#include "net/p2p/tracker.hpp"

using namespace net;
using namespace net::p2p;

peer_t *globl_peer;

void thread_main(u64 sid, socket_addr_t tserver_addr, microsecond_t timeout)
{
    event_context_t context(event_strategy::epoll);
    auto tracker_client = std::make_unique<tracker_node_client_t>();
    auto peer = std::make_unique<peer_t>(sid);

    tracker_client->config(false, sid, "");
    tracker_client->connect_server(context, tserver_addr, timeout);
    tracker_client->on_error([](tracker_node_client_t &c, socket_addr_t remote, connection_state state) {

    });

    context.run();
}

void init_peer(u64 sid, socket_addr_t tserver_addr, microsecond_t timeout)
{
    std::thread thread(std::bind(thread_main, sid, tserver_addr, timeout));
    thread.detach();
}

void close_peer() {}

void on_connect_error(std::function<bool()>) {}

void enable_share() {}

void disable_share() {}

void cancel_fragment(net::u64 fid, int channel) {}

net::socket_buffer_t get_fragment(u64 fid, int channel) { return net::socket_buffer_t(); }

net::socket_buffer_t get_meta(int key, int channel) { return net::socket_buffer_t(); }
