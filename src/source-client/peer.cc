#include "peer.hpp"
#include "net/event.hpp"
#include "net/p2p/peer.hpp"
#include "net/p2p/tracker.hpp"
#include "net/socket.hpp"
#include <glog/logging.h>
#include <thread>

using namespace net::p2p;
using namespace net;
event_context_t *app_context;
std::function<bool(net::connection_state)> error_handler;

peer_t *glob_peer;
peer_info_t *edge_peer_target = nullptr;

void thread_main(u64 sid, socket_addr_t ts_server_addr, microsecond_t timeout)
{
    event_context_t context(event_strategy::epoll);
    app_context = &context;
    auto peer = std::make_unique<peer_t>(sid);
    auto tracker = std::make_unique<tracker_node_client_t>();
    glob_peer = peer.get();
    tracker->config(false, sid, "");
    tracker->connect_server(*app_context, ts_server_addr, timeout);
    tracker->on_error([ts_server_addr, timeout](tracker_node_client_t &t, socket_addr_t addr, connection_state state) {
        if (error_handler)
        {
            if (error_handler(state))
            {
                event_loop_t::current().add_timer(make_timer(make_timespan(1), [&t, ts_server_addr, timeout]() {
                    t.connect_server(*app_context, ts_server_addr, timeout);
                }));
            }
            else
            {
                app_context->exit_all(-1);
            }
        }
    });

    tracker->on_tracker_server_connect([](tracker_node_client_t &c, socket_addr_t) {
        c.request_update_trackers();
        c.request_update_nodes(1, request_strategy::edge_node);
    });

    tracker->on_nodes_update([](tracker_node_client_t &c, peer_node_t *nodes, u64 count) {
        if (count <= 0)
        {
            if (error_handler)
            {
                if (error_handler(connection_state::connection_refuse))
                {
                    event_loop_t::current().add_timer(make_timer(
                        make_timespan(1), [&c]() { c.request_update_nodes(1, request_strategy::edge_node); }));
                }
                else
                {
                    app_context->exit_all(-1);
                }
            }
            return;
        }

        auto info = glob_peer->add_peer();
        glob_peer->connect_to_peer(info, socket_addr_t(nodes[0].ip, nodes[0].port));
    });

    peer->on_peer_connect([](peer_t &, peer_info_t *info) {
        if (edge_peer_target != nullptr)
        {
            LOG(ERROR) << "invalid state, new target " << info->remote_address.to_string();
        }
        edge_peer_target = info;
    });

    peer->on_peer_disconnect([](peer_t &, peer_info_t *info) {
        if (edge_peer_target != info)
        {
            LOG(ERROR) << "invalid state, new target " << info->remote_address.to_string();
        }
        edge_peer_target = nullptr;
    });

    context.run();
    glob_peer = nullptr;
    edge_peer_target = nullptr;
}

void init_peer(u64 sid, socket_addr_t ts_server_addr, microsecond_t timeout)
{
    std::thread thread(std::bind(thread_main, sid, ts_server_addr, timeout));
    thread.detach();
}

void send_data(void *buffer_ptr, int size, int channel, net::u64 fragment_id)
{
    if (glob_peer)
    {
        socket_buffer_t buffer(size);
        memcpy(buffer.get(), buffer_ptr, size);
        glob_peer->get_socket()->start_with([buffer, channel, fragment_id]() {
            glob_peer->send_fragment_to_peer(edge_peer_target, fragment_id, channel, buffer);
        });
    }
}

void send_meta_info(void *buffer_ptr, int size, int channel, int key)
{
    if (glob_peer)
    {
        socket_buffer_t buffer(size);
        memcpy(buffer.get(), buffer_ptr, size);
        glob_peer->get_socket()->start_with(
            [buffer, channel, key]() { glob_peer->send_meta_data_to_peer(edge_peer_target, key, channel, buffer); });
    }
}

void on_connection_error(std::function<bool(net::connection_state)> func) { error_handler = func; }

void close_peer() { app_context->exit_all(0); }
