#pragma once
#include "event.hpp"
#include "socket_addr.hpp"
#include "socket_buffer.hpp"
#include <functional>

namespace net
{
class socket_t;
};

namespace net::udp
{
class client_t;
class server_t;
using server_at_connect_handler = std::function<void(server_t &)>;

class server_t
{
    socket_t *socket;
    event_context_t *context;

  public:
    ~server_t();
    socket_t *get_socket() const { return socket; }
    socket_t *bind(event_context_t &context, socket_addr_t addr, bool reuse_port = false);
    void run(std::function<void()> func);
    void close();
};

class client_t
{
    socket_t *socket;
    socket_addr_t connect_addr;
    event_context_t *context;

  private:
  public:
    ~client_t();
    socket_t *get_socket() const { return socket; }

    void connect(event_context_t &context, socket_addr_t addr, bool remote_address_bind_to_socket = true);
    void run(std::function<void()> func);
    void close();
    socket_addr_t get_address() const;
};

class connectable_server_t;
using msg_recv_handler_t = std::function<void(connectable_server_t &, socket_buffer_t, socket_addr_t)>;

class connectable_server_t
{
    socket_t *socket;
    event_context_t *context;

  private:
    void co_main(msg_recv_handler_t handler, int max_message_size);

  public:
    ~connectable_server_t();
    ///\brief configure udp server create a new coroutine for each client
    void listen_message_recv(msg_recv_handler_t handler, int max_message_size = 1472);
    socket_t *get_socket() const { return socket; }

    socket_t *bind(event_context_t &context, socket_addr_t addr);
    void exit_client(client_t &client);
    void close();
};

} // namespace net::udp
