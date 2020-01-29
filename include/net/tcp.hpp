#pragma once
#include "socket_addr.hpp"
#include <functional>

namespace net
{
class event_context_t;
class socket_t;
}; // namespace net

namespace net::tcp
{
class server_t;
using handler_t = std::function<void(server_t &, socket_t *)>;
class server_t
{
    socket_t *server_socket;
    event_context_t *context;
    handler_t join_handler;
    handler_t exit_handler;

  private:
  public:
    void listen(event_context_t &context, socket_addr_t address, int max_client);
    void at_client_join(handler_t handler);
    void at_client_exit(handler_t handler);
    void close();
};

class client_t
{
    socket_t *socket;

  public:
    void connect(event_context_t &context, socket_addr_t server_address);

    void close(event_context_t &context);
};

} // namespace net::tcp
