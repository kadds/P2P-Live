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

    peer_server_t server;

    int ok = 0;

    server.on_client_join([&ok](peer_server_t &server, speer_t *speer) { ok++; });

    peer_client_t client(1);

    client.on_peer_disconnect([](peer_client_t &client, peer_t *peer) {
        std::string str = "peer client connect error";
        GTEST_ASSERT_EQ("", str);
    });

    client.on_peer_connect([&ok, &loop](peer_client_t &client, peer_t *peer) {
        ok++;
        loop.exit(0);
    });
    auto client_peer = client.add_peer(context);
    auto client_addr = client_peer->udp.get_socket()->local_addr();

    auto server_peer = server.add_peer(context, socket_addr_t("127.0.0.1", client_addr.get_port()));
    auto server_addr = server_peer->udp.get_socket()->local_addr();
    client.connect_to_peer(client_peer, socket_addr_t("127.0.0.1", server_addr.get_port()));

    loop.add_timer(make_timer(net::make_timespan(1), [&loop]() { loop.exit(-1); }));
    loop.run();
    GTEST_ASSERT_EQ(ok, 2);
}

TEST(PeerTest, DataTransport)
{
    event_context_t context(event_strategy::epoll);
    event_loop_t loop;
    context.add_event_loop(&loop);

    peer_server_t server;

    server.on_client_pull([](peer_server_t &server, peer_data_request_t &request, speer_t *peer) {
        if (request.data_id == 1)
        {
            socket_buffer_t buffer(2);
            buffer.expect().origin_length();
            buffer.get_raw_ptr()[0] = 0xFE;
            buffer.get_raw_ptr()[1] = 0xA0;
            server.send_package_to_peer(peer, 1, std::move(buffer));
        }
        GTEST_ASSERT_EQ(request.data_id, 1);
    });

    peer_client_t client(1);

    client.on_peer_disconnect([](peer_client_t &client, peer_t *peer) {
        std::string str = "peer client connect error";
        GTEST_ASSERT_EQ("", str);
    });

    client.on_peer_connect([](peer_client_t &client, peer_t *peer) { client.pull_data_from_peer(1); });

    bool data_recved = false;
    client.on_peer_data_recv([&loop, &data_recved](peer_client_t &client, peer_data_package_t &data, peer_t *peer) {
        GTEST_ASSERT_EQ(data.size, 2);
        GTEST_ASSERT_EQ(data.data[0], 0xFE);
        GTEST_ASSERT_EQ(data.data[1], 0xA0);
        data_recved = true;
        loop.exit(0);
    });

    auto client_peer = client.add_peer(context);
    auto client_addr = client_peer->udp.get_socket()->local_addr();

    auto server_peer = server.add_peer(context, socket_addr_t("127.0.0.1", client_addr.get_port()));
    auto server_addr = server_peer->udp.get_socket()->local_addr();
    client.connect_to_peer(client_peer, socket_addr_t("127.0.0.1", server_addr.get_port()));

    loop.add_timer(make_timer(net::make_timespan(1), [&loop]() { loop.exit(-1); }));
    loop.run();
    GTEST_ASSERT_EQ(data_recved, true);
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