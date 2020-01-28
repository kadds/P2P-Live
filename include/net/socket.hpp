#pragma once
#include "event.hpp"
#include "net.hpp"
#include "socket_addr.hpp"
#include "socket_buffer.hpp"

namespace net
{
enum io_result
{
    ok,
    in_process,
    continu,
    closed,
    timeout,
    failed,
};

class socket_t
{
    int fd;
    socket_addr_t local;
    socket_addr_t remote;
    std::list<event_handler_t> event_handles;

  public:
    socket_t(int fd);
    ~socket_t();

    io_result write(socket_buffer_t &buffer);
    io_result read(socket_buffer_t &buffer);

    void write_async(socket_buffer_t &buffer);
    void read_async(socket_buffer_t &buffer);

    io_result write_pack(socket_buffer_t &buffer, socket_addr_t target);
    io_result read_pack(socket_buffer_t &buffer, socket_addr_t target);

    socket_addr_t local_addr();
    socket_addr_t remote_addr();

    int get_raw_handle() const { return fd; }

    void add_handler(event_handler_t handler);
    void on_event(event_context_t &context, event_type_t type);
};

socket_t *connect_to(socket_addr_t socket_to);
socket_t *listen_from(socket_addr_t socket_in, int max_count);
socket_t *accept_from(socket_t *in);
void close_socket(socket_t *socket);

} // namespace net
