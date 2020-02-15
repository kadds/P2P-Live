#include "net/p2p/peer.hpp"
#include "net/event.hpp"
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

    server.at_client_join([&ok](peer_server_t &server, speer_t *speer) { ok++; });

    peer_client_t client(1);

    client.at_peer_disconnect([](peer_client_t &client, peer_t *peer) {
        std::string str = "peer client connect error";
        GTEST_ASSERT_EQ("", str);
    });

    client.at_peer_connect([&ok, &loop](peer_client_t &client, peer_t *peer) {
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

    server.at_client_pull([](peer_server_t &server, peer_data_request_t &request, speer_t *peer) {
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

    client.at_peer_disconnect([](peer_client_t &client, peer_t *peer) {
        std::string str = "peer client connect error";
        GTEST_ASSERT_EQ("", str);
    });

    client.at_peer_connect([](peer_client_t &client, peer_t *peer) { client.pull_data_from_peer(1); });

    bool data_recved = false;
    client.at_peer_data_recv([&loop, &data_recved](peer_client_t &client, peer_data_package_t &data, peer_t *peer) {
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
