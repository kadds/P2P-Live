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

class server_t
{
  private:
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
  private:
    socket_t *socket;
    socket_addr_t connect_addr;
    event_context_t *context;

  public:
    ~client_t();
    socket_t *get_socket() const { return socket; }

    /// it just bind socket to a event, not really connect. set remote_address_bind_to_socket true can connect an
    /// address
    void connect(event_context_t &context, socket_addr_t addr, bool remote_address_bind_to_socket = true);

    /// must call 'connect' befor call it.
    ///
    void run(std::function<void()> func);
    void close();
    socket_addr_t get_address() const;
};

} // namespace net::udp
