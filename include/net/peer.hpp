#pragma once
#include "endian.hpp"
#include "socket_addr.hpp"
#include "tcp.hpp"
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace net
{
class socket_t;

} // namespace net
namespace net::peer
{
class peer_t
{
};
class peer_client_t;
using connect_peer_server_handler_t = std::function<void(peer_client_t &, socket_t *)>;

class peer_client_t
{
  private:
    bool in_peer_network;
    int room_id;
    std::unordered_set<peer_t *> map;
    tcp::client_t client, room_client;
    connect_peer_server_handler_t handler, error_handler;

    void front_server_main(event_context_t &context, tcp::client_t &client, socket_t *socket);

  public:
    peer_client_t();
    ~peer_client_t();
    peer_client_t(const peer_client_t &) = delete;
    peer_client_t &operator=(const peer_client_t &) = delete;

    void join_peer_network(event_context_t &context, socket_addr_t server_addr, int room_id);
    bool is_in_network() const;
    peer_client_t &at_connnet_peer_server(connect_peer_server_handler_t handler);
    peer_client_t &at_connect_peer_server_error(connect_peer_server_handler_t handler);
};
class peer_server_t;

using front_server_connect_handler_t = std::function<void(bool, socket_t *)>;
using client_join_handler_t = std::function<void(peer_server_t &, socket_t *)>;

class peer_server_t
{
  private:
    tcp::client_t client;
    tcp::server_t server;
    front_server_connect_handler_t front_server_handler;
    client_join_handler_t client_handler;

  public:
    void connect_to_front_server(event_context_t &context, socket_addr_t addr);
    void bind_server(event_context_t &context, socket_addr_t addr, bool reuse_addr = false);
    peer_server_t &at_client_join(client_join_handler_t handler);
    peer_server_t &at_front_server_connect(front_server_connect_handler_t handler);
};

} // namespace net::peer