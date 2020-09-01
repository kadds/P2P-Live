#include "net/p2p/peer.hpp"
#include "net/event.hpp"
#include "net/p2p/tracker.hpp"
#include "net/socket.hpp"
#include <gtest/gtest.h>

using namespace net::p2p;
using namespace net;

TEST(PeerTest, PeerConnection)
{
    event_context_t ctx(event_strategy::AUTO);

    peer_t server(1), client(1);

    int ok = 0;

    server.on_peer_connect([&ok, &ctx](peer_t &server, peer_info_t *peer) {
        ok++;
        if (ok >= 2)
            ctx.exit_all(0);
    });

    client.on_peer_connect([&ok, &ctx](peer_t &client, peer_info_t *peer) {
        ok++;
        if (ok >= 2)
            ctx.exit_all(0);
    });
    client.bind(ctx);
    server.bind(ctx);

    auto server_peer = client.add_peer();
    auto client_peer = server.add_peer();

    client.connect_to_peer(server_peer, socket_addr_t("127.0.0.1", server.get_socket()->local_addr().get_port()));
    server.connect_to_peer(client_peer, socket_addr_t("127.0.0.1", client.get_socket()->local_addr().get_port()));

    event_loop_t::current().add_timer(make_timer(net::make_timespan(2), [&ctx]() { ctx.exit_all(-1); }));
    ctx.run();
    GTEST_ASSERT_EQ(ok, 2);
}

TEST(PeerTest, DataTransport)
{
    constexpr u64 test_size_bytes = 4096;
    event_context_t ctx(event_strategy::AUTO);

    peer_t server(1), client(1);

    std::string name = "test string";
    server.accept_channels({1});
    client.accept_channels({1});

    server
        .on_meta_pull_request([&name, test_size_bytes](peer_t &server, peer_info_t *peer, u64 key, int channel) {
            socket_buffer_t buffer = socket_buffer_t::from_string(name);
            buffer.expect().origin_length();
            server.send_meta_data_to_peer(peer, key, 1, std::move(buffer));
        })
        .on_fragment_pull_request([test_size_bytes](peer_t &server, peer_info_t *peer, fragment_id_t fid, int channel) {
            GTEST_ASSERT_GE(fid, 1);
            GTEST_ASSERT_LE(fid, 2);
            socket_buffer_t buffer(test_size_bytes);
            buffer.expect().origin_length();
            buffer.clear();
            buffer.get()[test_size_bytes - 1] = fid;
            server.send_fragment_to_peer(peer, fid, 1, std::move(buffer));
        });

    int x = 0;
    client.on_peer_connect([](peer_t &client, peer_info_t *peer) { client.pull_meta_data(peer, 0, 1); })
        .on_meta_data_recv(
            [&ctx, &name](peer_t &client, peer_info_t *peer, socket_buffer_t buffer, u64 key, int channel) {
                GTEST_ASSERT_EQ(key, 0);
                GTEST_ASSERT_EQ(buffer.get_length(), name.size());
                std::string str = buffer.to_string();
                GTEST_ASSERT_EQ(str, name);
                client.pull_fragment_from_peer(peer, {1, 2}, 1, 0);
            })
        .on_fragment_recv([&ctx, &name, &x, test_size_bytes](peer_t &client, peer_info_t *peer, socket_buffer_t buffer,
                                                             fragment_id_t id, int channel) {
            GTEST_ASSERT_EQ(buffer.get_length(), test_size_bytes);
            GTEST_ASSERT_EQ(buffer.get()[test_size_bytes - 1], id);
            x++;
            if (x >= 2)
            {
                ctx.exit_all(0);
            }
        });

    client.bind(ctx);
    server.bind(ctx);

    auto server_peer = client.add_peer();
    auto client_peer = server.add_peer();

    client.connect_to_peer(server_peer, socket_addr_t("127.0.0.1", server.get_socket()->local_addr().get_port()));
    server.connect_to_peer(client_peer, socket_addr_t("127.0.0.1", client.get_socket()->local_addr().get_port()));

    event_loop_t::current().add_timer(make_timer(net::make_timespan(2), [&ctx]() { ctx.exit_all(-1); }));
    GTEST_ASSERT_EQ(ctx.run(), 0);
    GTEST_ASSERT_EQ(x, 2);
}

TEST(PeerTest, TrackerPingPong)
{
    constexpr int test_count = 5;
    event_context_t ctx(event_strategy::AUTO);

    bool ok = false;
    std::unique_ptr<tracker_server_t[]> servers = std::make_unique<tracker_server_t[]>(test_count);
    socket_addr_t addrs[test_count];

    for (auto i = 0; i < test_count; i++)
    {
        addrs[i] = socket_addr_t("127.0.0.1", 25000 + i);
        servers[i].config("");
        servers[i].bind(ctx, addrs[i], 200, true);
    }

    for (auto i = 0; i < test_count; i++)
    {
        for (auto j = i + 1; j < test_count; j++)
        {
            servers[i].link_other_tracker_server(ctx, addrs[j], make_timespan(1));
        }
    }

    event_loop_t::current().add_timer(make_timer(net::make_timespan(4), [&ctx, &servers, &addrs, test_count]() {
        for (auto i = 0; i < test_count; i++)
        {
            auto peer = servers[i].get_trackers();
            GTEST_ASSERT_EQ(peer.size(), test_count - 1);
            for (auto j = 0; j < peer.size(); j++)
            {
                GTEST_ASSERT_GE(peer[j].port, 25000);
                GTEST_ASSERT_LT(peer[j].port, 25000 + test_count);
            }
        }
        ctx.exit_all(0);
    }));
    event_loop_t::current().add_timer(make_timer(net::make_timespan(5), [&ctx]() { ctx.exit_all(-1); }));
    GTEST_ASSERT_EQ(ctx.run(), 0);
}

TEST(PeerTest, TrackerNode)
{
    constexpr int test_count = 4;
    event_context_t ctx(event_strategy::AUTO);

    tracker_server_t tserver1;
    socket_addr_t taddrs1("127.0.0.1", 2555);
    tserver1.bind(ctx, taddrs1, test_count + 2, true);
    tracker_server_t tserver2;
    socket_addr_t taddrs2("127.0.0.1", 2556);
    tserver2.bind(ctx, taddrs2, test_count + 2, true);

    // link tserver1 and tserver2
    tserver1.link_other_tracker_server(ctx, taddrs2, make_timespan(1));

    std::unique_ptr<tracker_node_client_t[]> tclients = std::make_unique<tracker_node_client_t[]>(test_count);
    int update_cnt = 0;
    for (int i = 0; i < test_count; i++)
    {
        tclients[i].config(true, 1, "");
        tclients[i].connect_server(ctx, taddrs1, make_timespan_full());
        tclients[i].on_nodes_update([test_count](tracker_node_client_t &client, peer_node_t *nodes, u64 count) {
            GTEST_ASSERT_EQ(count, test_count);
        });
        tclients[i].on_trackers_update(
            [taddrs2, &update_cnt](tracker_node_client_t &, tracker_node_t *nodes, u64 count) {
                /// always get tserver2 address
                GTEST_ASSERT_EQ(count, 1);
                GTEST_ASSERT_EQ(nodes[0].port, taddrs2.get_port());
                update_cnt++;
            });
    }

    event_loop_t::current().add_timer(make_timer(make_timespan(1), [&tclients, test_count]() {
        for (int i = 0; i < test_count; i++)
        {
            tclients[i].request_update_nodes(1000, RequestNodeStrategy::random);
            tclients[i].request_update_trackers();
        }
    }));

    event_loop_t::current().add_timer(make_timer(net::make_timespan(2), [&ctx]() { ctx.exit_all(0); }));
    GTEST_ASSERT_EQ(ctx.run(), 0);
    GTEST_ASSERT_EQ(update_cnt, test_count);
}

struct peer_hash_func_t
{
    u64 operator()(const socket_addr_t &addr) const { return addr.hash(); }
};

void static peer_main(event_context_t &ctx, socket_addr_t taddr, tracker_node_client_t &client, peer_t &peer,
                      int &flags, std::unordered_map<socket_addr_t, peer_info_t *, peer_hash_func_t> &remote_peers)
{
    peer.bind(ctx);
    client.config(true, 30, "");
    client.connect_server(ctx, taddr, make_timespan_full());

    client
        .on_nodes_update([&peer, &remote_peers](tracker_node_client_t &client, peer_node_t *nodes, u64 count) {
            for (auto i = 0; i < count; i++)
            {
                client.request_connect_node(nodes[i], peer.get_udp());
            }
        })
        .on_node_request_connect([&peer, &remote_peers](tracker_node_client_t &client, peer_node_t node) {
            socket_addr_t addr(node.ip, node.udp_port);
            // has connected ?
            if (remote_peers[addr] != nullptr)
                return;

            peer.connect_to_peer(peer.add_peer(), addr);
            client.request_connect_node(node, peer.get_udp());
        });
    peer.on_peer_connect([&flags, &ctx, &remote_peers](peer_t &peer, peer_info_t *p) {
        remote_peers[p->remote_address] = p;
        flags++;
        if (flags > 1)
            ctx.exit_all(0);
    });
}

TEST(PeerTest, NATSend)
{
    event_context_t context(event_strategy::AUTO);

    tracker_server_t tserver1;
    socket_addr_t taddrs1("127.0.0.1", 2558);
    tserver1.config("");

    tserver1.bind(context, taddrs1, true);

    tracker_node_client_t client;
    peer_t peer(10);

    tracker_node_client_t client2;
    peer_t peer2(10);
    std::unordered_map<socket_addr_t, peer_info_t *, peer_hash_func_t> remote_peers;
    std::unordered_map<socket_addr_t, peer_info_t *, peer_hash_func_t> remote_peers2;

    int flags;

    peer_main(context, taddrs1, client, peer, flags, remote_peers);
    peer_main(context, taddrs1, client2, peer2, flags, remote_peers2);

    event_loop_t::current().add_timer(make_timer(
        net::make_timespan(0, 500), [&client]() { client.request_update_nodes(10, RequestNodeStrategy::random); }));

    event_loop_t::current().add_timer(make_timer(net::make_timespan(3), [&context]() { context.exit_all(-1); }));
    GTEST_ASSERT_EQ(context.run(), 0);
}
