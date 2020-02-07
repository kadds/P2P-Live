#include "net/peer.hpp"
#include "net/event.hpp"
#include "net/load_balance.hpp"
#include "net/socket.hpp"
#include <gtest/gtest.h>

using namespace net::peer;
using namespace net;

TEST(PeerTest, Base)
{
    event_context_t context(event_strategy::epoll);
    event_loop_t loop;
    context.add_event_loop(&loop);

    socket_addr_t front_addr(1244);
    socket_addr_t front_inner_addr(1330);

    socket_addr_t server0_addr(1288);

    peer_server_t server0;

    load_balance::front_server_t front_server;

    socket_addr_t least_server = server0_addr;

    front_server.at_client_request([&least_server](load_balance::front_server_t &server,
                                                   const peer_get_server_request_t &request,
                                                   peer_get_server_respond_t &respond, socket_t *client) -> bool {
        if (request.room_id > 0 && request.room_id < 1000 && request.version == 1)
        {
            /// tell client the context server address and port
            respond.ip_addr = least_server.v4_addr();
            respond.port = least_server.get_port();
            respond.version = 1;
            respond.state = 0;
            respond.session_id = request.room_id * 100;
            return true;
        }
        [&request]() { GTEST_ASSERT_EQ(request.room_id, 100); }();
        return false;
    });

    front_server.at_inner_server_join([](load_balance::front_server_t &server, socket_t *client) {

    });

    front_server.bind_inner(context, front_inner_addr, true);
    front_server.bind(context, front_addr, true);

    server0.at_front_server_connect([](bool ok, socket_t *socket) { GTEST_ASSERT_EQ(ok, true); });

    server0.bind_server(context, server0_addr, true);
    server0.connect_to_front_server(context, front_inner_addr);
    bool ok = false;

    peer_client_t client;
    client.at_connnet_peer_server([&ok](peer_client_t &client, socket_t *socket) {
        ok = true;
        GTEST_LOG_(INFO) << "connect ok";
    });

    client.at_connect_peer_server_error([](peer_client_t &client, socket_t *socket) {
        std::string str = "peer client connect error";
        GTEST_ASSERT_EQ("", str);
    });
    client.join_peer_network(context, front_addr, 10);

    loop.add_timer(make_timer(1000000, [&loop]() { loop.exit(0); }));
    loop.run();
    GTEST_ASSERT_EQ(ok, true);
}

TEST(PeerTest, DataTransport) {}
