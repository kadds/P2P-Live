#include "net/p2p/peer.hpp"
#include "net/event.hpp"
#include "net/p2p/tracker.hpp"
#include "net/socket.hpp"
#include <gtest/gtest.h>

using namespace net::p2p;
using namespace net;

TEST(PeerTest, PeerConnection)
{
    event_context_t context(event_strategy::epoll);
    event_loop_t loop;
    context.add_event_loop(&loop);

    peer_t server(1), client(1);

    int ok = 0;

    server.on_peer_connect([&ok, &loop](peer_t &server, peer_info_t *peer) {
        ok++;
        if (ok >= 2)
            loop.exit(0);
    });

    client.on_peer_connect([&ok, &loop](peer_t &client, peer_info_t *peer) {
        ok++;
        if (ok >= 2)
            loop.exit(0);
    });
    client.bind(context);
    server.bind(context);

    auto server_peer = client.add_peer();
    auto client_peer = server.add_peer();

    client.connect_to_peer(server_peer, socket_addr_t("127.0.0.1", server.get_socket()->local_addr().get_port()));
    server.connect_to_peer(client_peer, socket_addr_t("127.0.0.1", client.get_socket()->local_addr().get_port()));

    loop.add_timer(make_timer(net::make_timespan(1), [&loop]() { loop.exit(-1); }));
    loop.run();
    GTEST_ASSERT_EQ(ok, 2);
}

TEST(PeerTest, DataTransport)
{
    constexpr u64 test_size_bytes = 4096;
    event_context_t context(event_strategy::epoll);
    event_loop_t loop;
    context.add_event_loop(&loop);

    peer_t server(1), client(1);

    std::string name = "test string";
    server.accept_channels({1});
    client.accept_channels({1});

    server
        .on_meta_pull_request([&name, test_size_bytes](peer_t &server, peer_info_t *peer, u64 key) {
            socket_buffer_t buffer(name);
            buffer.expect().origin_length();
            server.send_meta_data_to_peer(peer, key, 1, std::move(buffer));
        })
        .on_fragment_pull_request([](peer_t &server, peer_info_t *peer, fragment_id_t fid) {
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
        .on_meta_data_recv([&loop, &name](peer_t &client, socket_buffer_t &buffer, u64 key, peer_info_t *peer) {
            GTEST_ASSERT_EQ(key, 0);
            GTEST_ASSERT_EQ(buffer.get_length(), name.size());
            std::string str = buffer.to_string();
            GTEST_ASSERT_EQ(str, name);
            client.pull_fragment_from_peer(peer, {1, 2}, 1, 0);
        })
        .on_fragment_recv([&loop, &name, &x, test_size_bytes](peer_t &client, socket_buffer_t &buffer, fragment_id_t id,
                                                              peer_info_t *peer) {
            GTEST_ASSERT_EQ(buffer.get_length(), test_size_bytes);
            GTEST_ASSERT_EQ(buffer.get()[test_size_bytes - 1], id);
            x++;
            if (x >= 2)
            {
                loop.exit(0);
            }
        });

    client.bind(context);
    server.bind(context);

    auto server_peer = client.add_peer();
    auto client_peer = server.add_peer();

    client.connect_to_peer(server_peer, socket_addr_t("127.0.0.1", server.get_socket()->local_addr().get_port()));
    server.connect_to_peer(client_peer, socket_addr_t("127.0.0.1", client.get_socket()->local_addr().get_port()));

    // loop.add_timer(make_timer(net::make_timespan(2), [&loop]() { loop.exit(-1); }));
    loop.run();
    GTEST_ASSERT_EQ(x, 2);
}

TEST(PeerTest, TrackerPingPong)
{
    constexpr int test_count = 25;
    event_context_t context(event_strategy::epoll);
    event_loop_t loop;
    context.add_event_loop(&loop);

    bool ok = false;
    std::unique_ptr<tracker_server_t[]> servers = std::make_unique<tracker_server_t[]>(test_count);
    socket_addr_t addrs[test_count];

    for (auto i = 0; i < test_count; i++)
    {
        addrs[i] = socket_addr_t("127.0.0.1", 2500 + i);
        servers[i].bind(context, addrs[i], true);
    }

    for (auto i = 0; i < test_count; i++)
    {
        for (auto j = i + 1; j < test_count; j++)
        {
            servers[i].link_other_tracker_server(context, addrs[j], make_timespan_full());
        }
    }

    loop.add_timer(make_timer(net::make_timespan(2), [&loop, &servers, &addrs]() {
        for (auto i = 0; i < test_count; i++)
        {
            auto peer = servers[i].get_trackers();
            GTEST_ASSERT_EQ(peer.size(), test_count - 1);
            for (auto j = 0; j < peer.size(); j++)
            {
                GTEST_ASSERT_GE(peer[j].port, 2500);
                GTEST_ASSERT_LT(peer[j].port, 2500 + test_count);
            }
        }
        loop.exit(-1);
    }));
    loop.run();
}

TEST(PeerTest, TrackerNode)
{
    constexpr int test_count = 25;
    event_context_t context(event_strategy::epoll);
    event_loop_t loop;
    context.add_event_loop(&loop);

    tracker_server_t tserver1;
    socket_addr_t taddrs1("127.0.0.1", 2555);
    tserver1.bind(context, taddrs1, true);
    tracker_server_t tserver2;
    socket_addr_t taddrs2("127.0.0.1", 2556);
    tserver2.bind(context, taddrs2, true);

    // link tserver1 and tserver2
    tserver1.link_other_tracker_server(context, taddrs2, make_timespan(1));

    std::unique_ptr<tracker_node_client_t[]> tclients = std::make_unique<tracker_node_client_t[]>(test_count);
    for (int i = 0; i < test_count; i++)
    {
        tclients[i].config(1, 30, p2p::request_strategy::random);
        tclients[i].connect_server(context, taddrs1, make_timespan_full());
        tclients[i].on_nodes_update([](tracker_node_client_t &client, peer_node_t *nodes, u64 count) {
            GTEST_ASSERT_EQ(count, test_count - 1);
        });
        tclients[i].on_trackers_update([taddrs2](tracker_node_client_t &, tracker_node_t *nodes, u64 count) {
            /// always get tserver2 address
            GTEST_ASSERT_EQ(count, 1);
            GTEST_ASSERT_EQ(nodes[0].port, taddrs2.get_port());
        });
    }

    loop.add_timer(make_timer(make_timespan(1), [&tclients]() {
        for (int i = 0; i < test_count; i++)
        {
            tclients[i].request_update_nodes();
            tclients[i].request_update_trackers();
        }
    }));

    loop.add_timer(make_timer(net::make_timespan(2), [&loop]() { loop.exit(0); }));
    loop.run();
}

TEST(PeerTest, Hole)
{
    constexpr int test_count = 25;
    event_context_t context(event_strategy::epoll);
    event_loop_t loop;
    context.add_event_loop(&loop);

    tracker_server_t tserver1;
    socket_addr_t taddrs1("127.0.0.1", 2555);
    tserver1.bind(context, taddrs1, true);

    tracker_node_client_t client1;
    peer_t peer1(1);

    tracker_node_client_t client2;
    peer_t peer2(1);

    peer1.bind(context);
    peer2.bind(context);

    client1.config(1, 30, p2p::request_strategy::random);
    client1.connect_server(context, taddrs1, make_timespan_full());

    client1.on_nodes_update([&peer1](tracker_node_client_t &client, peer_node_t *nodes, u64 count) {
        client.request_connect_node(nodes[0], peer1.get_udp());
    });

    client2.config(1, 30, p2p::request_strategy::random);
    client2.connect_server(context, taddrs1, make_timespan_full());
    client2.on_node_request_connect([&peer2](tracker_node_client_t &client, peer_node_t node, u16 udp_port) {
        // peer2.connect_to_peer();
    });

    loop.add_timer(make_timer(net::make_timespan(0, 500), [&client1]() { client1.request_update_nodes(); }));

    loop.add_timer(make_timer(net::make_timespan(2), [&loop]() { loop.exit(0); }));
    loop.run();
}
