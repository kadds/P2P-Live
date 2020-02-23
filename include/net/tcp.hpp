#pragma once
#include "co.hpp"
#include "endian.hpp"
#include "socket_addr.hpp"
#include "socket_buffer.hpp"
#include "timer.hpp"
#include <functional>

namespace net
{
class event_context_t;
class socket_t;
}; // namespace net

namespace net::tcp
{
/// tcp application head
#pragma pack(push, 1)

struct package_head_v1_t
{
    u8 version;
    u16 size; /// a package size(byte) exclude this struct
    using member_list_t = serialization::typelist_t<u8, u16>;
};

struct package_head_v2_t
{
    u8 version;
    u32 size;
    using member_list_t = serialization::typelist_t<u8, u32>;
};

struct package_head_v3_t
{
    u8 version;
    u16 size;
    u8 msg_type;
    using member_list_t = serialization::typelist_t<u8, u16, u8>;
};

struct package_head_v4_t
{
    u8 version;
    u32 size;
    u16 msg_type;
    using member_list_t = serialization::typelist_t<u8, u32, u16>;
};

#pragma pack(pop)

struct package_head_t
{
    union
    {
        u8 version;
        package_head_v1_t v1;
        package_head_v2_t v2;
        package_head_v3_t v3;
        package_head_v4_t v4;
    };
};

class connection_t
{
    socket_t *socket;

  public:
    connection_t(socket_t *so)
        : socket(so){};
    /// async write data by stream mode
    co::async_result_t<io_result> awrite(co::paramter_t &param, socket_buffer_t &buffer);
    /// async read data by stream mode
    co::async_result_t<io_result> aread(co::paramter_t &param, socket_buffer_t &buffer);

    /// wait next package and read tcp application head
    co::async_result_t<io_result> aread_packet_head(co::paramter_t &param, package_head_t &head);

    co::async_result_t<io_result> aread_packet_content(co::paramter_t &param, socket_buffer_t &buffer);

    /// write package
    co::async_result_t<io_result> awrite_packet(co::paramter_t &param, package_head_t &head, socket_buffer_t &buffer);

    socket_t *get_socket() { return socket; }
};

/// wrappers
co::async_result_t<io_result> conn_awrite(co::paramter_t &param, connection_t conn, socket_buffer_t &buffer);
co::async_result_t<io_result> conn_aread(co::paramter_t &param, connection_t conn, socket_buffer_t &buffer);
co::async_result_t<io_result> conn_aread_packet_head(co::paramter_t &param, connection_t conn, package_head_t &head);
co::async_result_t<io_result> conn_aread_packet_content(co::paramter_t &param, connection_t conn,
                                                        socket_buffer_t &buffer);
co::async_result_t<io_result> conn_awrite_packet(co::paramter_t &param, connection_t conn, package_head_t &head,
                                                 socket_buffer_t &buffer);

class server_t
{
  public:
    using handler_t = std::function<void(server_t &, connection_t)>;
    using error_handler_t = std::function<void(server_t &, socket_t *, socket_addr_t, connection_state)>;

  private:
    socket_t *server_socket;
    event_context_t *context;
    handler_t join_handler;
    handler_t exit_handler;
    error_handler_t error_handler;

  private:
    void wait_client();
    void client_main(socket_t *socket);

  public:
    server_t();
    ~server_t();
    void listen(event_context_t &context, socket_addr_t address, int max_client, bool reuse_addr = false);
    server_t &on_client_join(handler_t handler);
    server_t &on_client_exit(handler_t handler);
    server_t &on_client_error(error_handler_t handler);

    void exit_client(socket_t *client);
    void close_server();
    socket_t *get_socket() const { return server_socket; }
};

class client_t
{
  public:
    using handler_t = std::function<void(client_t &, connection_t)>;
    using error_handler_t = std::function<void(client_t &, socket_t *, socket_addr_t, connection_state)>;

  private:
    socket_t *socket;
    socket_addr_t connect_addr;
    handler_t join_handler;
    handler_t exit_handler;
    error_handler_t error_handler;
    event_context_t *context;
    void wait_server(socket_addr_t address, microsecond_t timeout);

  public:
    client_t();
    ~client_t();
    void connect(event_context_t &context, socket_addr_t server_address, microsecond_t timeout);
    client_t &on_server_connect(handler_t handler);
    client_t &on_server_disconnect(handler_t handler);
    client_t &on_server_error(error_handler_t handler);

    void close();

    socket_addr_t get_connect_addr() const { return connect_addr; }
    socket_t *get_socket() const { return socket; }
    tcp::connection_t get_connection() const { return socket; }
    bool is_connect() const;
};

} // namespace net::tcp
