#pragma once
#include "socket_addr.hpp"
#include "timer.hpp"
#include <functional>

namespace net
{
class event_context_t;
class socket_t;
}; // namespace net

namespace net::tcp
{
class server_t;
using server_handler_t = std::function<void(server_t &, socket_t *)>;
using server_error_handler_t = std::function<void(server_t &, socket_t *, connection_state)>;

class server_t
{
    socket_t *server_socket;
    event_context_t *context;
    server_handler_t join_handler;
    server_handler_t exit_handler;
    server_error_handler_t error_handler;

  private:
    void wait_client();
    void client_main(socket_t *socket);

  public:
    server_t();
    ~server_t();
    void listen(event_context_t &context, socket_addr_t address, int max_client, bool reuse_addr = false);
    server_t &at_client_join(server_handler_t handler);
    server_t &at_client_exit(server_handler_t handler);
    server_t &at_client_connection_error(server_error_handler_t handler);

    void exit_client(socket_t *client);
    void close_server();
};
class client_t;

using client_handler_t = std::function<void(client_t &, socket_t *)>;

using client_error_handler_t = std::function<void(client_t &, socket_t *, connection_state)>;

class client_t
{
    socket_t *socket;
    socket_addr_t connect_addr;
    client_handler_t join_handler;
    client_handler_t exit_handler;
    client_error_handler_t error_handler;
    event_context_t *context;
    void wait_server(socket_addr_t address, microsecond_t timeout);

  public:
    client_t();
    ~client_t();
    void connect(event_context_t &context, socket_addr_t server_address, microsecond_t timeout);
    client_t &at_server_connect(client_handler_t handler);
    client_t &at_server_disconnect(client_handler_t handler);
    client_t &at_server_connection_error(client_error_handler_t handler);

    void close();

    socket_addr_t get_connect_addr() const { return connect_addr; }
};

} // namespace net::tcp
