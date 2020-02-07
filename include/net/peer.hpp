#pragma once
#include "socket_addr.hpp"
#include <unordered_map>
namespace net::peer
{
class peer_token_t
{
    byte data[128];
};

class peer_t
{
  private:
    bool in_peer_network;
    int room_id;

  public:
    peer_t();
    ~peer_t();
    peer_t(const peer_t &) = delete;
    peer_t &operator=(const peer_t &) = delete;

    void join_peer_network(socket_addr_t server_addr, int room_id);
    bool is_in_network() const;
};

} // namespace net::peer