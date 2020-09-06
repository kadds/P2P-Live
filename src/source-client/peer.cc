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
std::function<void()> edge_server_prepared_handler;

peer_t *glob_peer;
peer_info_t *edge_peer_target = nullptr;

std::atomic_bool is_connect_edge_server = false;

void thread_main(u64 sid, socket_addr_t ts_server_addr, microsecond_t timeout)
{
    event_context_t context(event_strategy::AUTO);
    app_context = &context;
    auto peer = std::make_unique<peer_t>(sid);
    auto tracker = std::make_unique<tracker_node_client_t>();
    glob_peer = peer.get();
    tracker->config(false, sid, "");
    tracker->connect_server(*app_context, ts_server_addr, timeout);
    tracker->on_error([ts_server_addr, timeout](tracker_node_client_t &t, socket_addr_t addr, connection_state state) {
        is_connect_edge_server = false;

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
        c.request_update_nodes(2, RequestNodeStrategy::edge_nodes);
    });

    tracker->on_nodes_update([](tracker_node_client_t &c, peer_node_t *nodes, u64 count) {
        if (count <= 0)
        {
            if (error_handler)
            {
                if (error_handler(connection_state::connection_refuse))
                {
                    event_loop_t::current().add_timer(make_timer(
                        make_timespan(1), [&c]() { c.request_update_nodes(1, RequestNodeStrategy::edge_nodes); }));
                }
                else
                {
                    LOG(INFO) << "exit all context";
                    app_context->exit_all(-1);
                }
            }
            return;
        }
        auto addr = socket_addr_t(nodes[0].ip, nodes[0].udp_port);
        c.request_connect_node(nodes[0], glob_peer->get_udp());
    });
    peer->bind(context);
    peer->accept_channels({1, 2});
    peer->on_peer_connect([](peer_t &, peer_info_t *info) {
        LOG(INFO) << "peer server connect ok";
        if (edge_peer_target != nullptr)
        {
            LOG(ERROR) << "invalid state, new target " << info->remote_address.to_string();
        }
        edge_peer_target = info;
        is_connect_edge_server = true;
        if (edge_server_prepared_handler)
            edge_server_prepared_handler();
    });

    peer->on_peer_disconnect([](peer_t &, peer_info_t *info) {
        is_connect_edge_server = false;
        if (edge_peer_target != info)
        {
            LOG(ERROR) << "invalid state, new target " << info->remote_address.to_string();
        }
        if (error_handler && error_handler(connection_state::close_by_peer))
        {
            glob_peer->connect_to_peer(info, edge_peer_target->remote_address);
        }
        else
        {
            edge_peer_target = nullptr;
        }
    });
    LOG(INFO) << "udp bind at port " << peer->get_udp().get_socket()->local_addr().get_port();

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
    if (glob_peer && is_connect_edge_server)
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
    if (glob_peer && is_connect_edge_server)
    {
        socket_buffer_t buffer(size);
        memcpy(buffer.get(), buffer_ptr, size);
        glob_peer->get_socket()->start_with(
            [buffer, channel, key]() { glob_peer->send_meta_data_to_peer(edge_peer_target, key, channel, buffer); });
    }
}

bool is_connect() { return is_connect_edge_server; }

void on_connection_error(std::function<bool(net::connection_state)> func) { error_handler = func; }

void on_edge_server_prepared(std::function<void()> func) { edge_server_prepared_handler = func; }

void close_peer() { app_context->exit_all(0); }
