#include "net/peer.hpp"
#include "net/event.hpp"
#include "net/socket.hpp"
#include <gtest/gtest.h>

using namespace net::peer;
using namespace net;

TEST(PeerTest, PeerConnection)
{
    event_context_t context(event_strategy::epoll);
    event_loop_t loop;
    context.add_event_loop(&loop);

    socket_addr_t server_addr(1999);
    peer_server_t server;

    int ok = 0;

    server.bind_server(context, server_addr, true);
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

    auto peer = client.add_peer(context, server_addr);
    client.connect_to_peer(peer);

    loop.add_timer(make_timer(1000000, [&loop]() { loop.exit(-1); }));
    loop.run();
    GTEST_ASSERT_EQ(ok, 2);
}

TEST(PeerTest, DataTransport)
{
    event_context_t context(event_strategy::epoll);
    event_loop_t loop;
    context.add_event_loop(&loop);

    socket_addr_t server_addr(1999);
    peer_server_t server;

    server.at_request_data([](peer_server_t &server, peer_data_request_t &request, speer_t *peer) {
        if (request.data_id == 1)
        {
        }
    });

    server.bind_server(context, server_addr, true);

    peer_client_t client(1);

    client.at_peer_disconnect([](peer_client_t &client, peer_t *peer) {
        std::string str = "peer client connect error";
        GTEST_ASSERT_EQ("", str);
    });

    client.at_peer_connect([&loop](peer_client_t &client, peer_t *peer) { client.request_data_from_peer(1); });

    client.at_peer_data_recv([](peer_client_t &client, peer_data_package_t &data, peer_t *peer) {});

    auto peer = client.add_peer(context, server_addr);
    client.connect_to_peer(peer);

    loop.add_timer(make_timer(1000000, [&loop]() { loop.exit(-1); }));
    loop.run();
}
