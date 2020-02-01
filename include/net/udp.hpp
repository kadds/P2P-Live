#pragma once
#include "event.hpp"
#include "socket_addr.hpp"
#include <functional>

namespace net
{
class socket_t;
};

namespace net::udp
{
class client_t;
using handler_t = std::function<void(client_t &)>;

class server_t
{
    socket_t *socket;
    handler_t join_handler, exit_handler, error_handler;
    event_context_t *context;

  public:
    socket_t *get_socket() const { return socket; }
    socket_t *bind(event_context_t &context, socket_addr_t addr, bool reuse_port = false);

    ///\brief configure udp server create a new coroutine for each client
    void listen();
    server_t &at_client_join(handler_t handler);
    server_t &at_client_exit(handler_t handler);
    server_t &at_client_error(handler_t handler);
    void exit_client(client_t &client);
    void close();
};

class client_t
{
    socket_t *socket;
    socket_addr_t connect_addr;
    event_context_t *context;

  private:
    void co_main();

  public:
    socket_t *get_socket() const { return socket; }

    void connect(event_context_t &context, socket_addr_t addr, bool remote_address_bind_to_socket = true);
    void close();
    socket_addr_t get_address() const;
};

} // namespace net::udp
