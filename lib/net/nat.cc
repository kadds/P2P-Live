#include "net/nat.hpp"
#include "net/socket.hpp"
namespace net
{

void nat_server_t::client_main(tcp::connection_t conn)
{
    tcp::package_head_t head;
    if (co::await(tcp::conn_aread_packet_head, conn, head) != io_result::ok)
        return;
    if (head.version != 1)
    {
        return;
    }

    nat_request_t request;
    if (head.v3.size != sizeof(request))
        return;
    socket_buffer_t buffer = socket_buffer_t::from_struct(request);
    buffer.expect().origin_length();
    if (co::await(tcp::conn_aread_packet_content, conn, buffer) != io_result::ok)
        return;
    endian::cast_inplace(request, buffer);
}

void nat_server_t::other_server_main(tcp::connection_t conn) {}

void nat_server_t::bind(event_context_t &ctx, socket_addr_t server_addr, bool reuse_addr)
{
    server.on_client_join(std::bind(&nat_server_t::client_main, this, std::placeholders::_2));
    this->server.listen(ctx, server_addr, 100000, reuse_addr);
    rudp.bind(ctx);
    rudp.on_new_connection([this](rudp_connection_t conn) {
        socket_buffer_t buffer(rudp.get_mtu());
        buffer.expect().origin_length();
        co::await(rudp_aread, &rudp, conn, buffer);
    });
}

void nat_server_t::connect_second_server(event_context_t &ctx, socket_addr_t server_addr)
{
    other_server.on_server_connect(std::bind(&nat_server_t::other_server_main, this, std::placeholders::_2));
    other_server.connect(ctx, server_addr, make_timespan(10));
}

void net_detector_t::get_nat_type(event_context_t &ctx, socket_addr_t server, handler_t handler)
{
    if (is_do_request)
        return;
    key = rand();
    rudp.bind(ctx);
    rudp.on_new_connection([this, handler, server](rudp_connection_t conn) {
            if (conn.address == server)
            {
                nat_udp_request_t request;
                request.key = key;
                socket_buffer_t buffer = socket_buffer_t::from_struct(request);
                buffer.expect().origin_length();
                endian::cast_inplace(request, buffer);
                co::await(rudp_awrite, &rudp, conn, buffer);
            }

            socket_buffer_t buffer(rudp.get_mtu());
            buffer.expect().origin_length();
            co::await(rudp_aread, &rudp, conn, buffer);
            is_do_request = false;

            if (handler)
                handler(nat_type::unknown);

            is_do_request = false;
        })
        .on_connection_timeout([this, handler](rudp_connection_t conn) {
            is_do_request = false;

            if (handler)
                handler(nat_type::unknown);
        })
        .on_unknown_packet([this](socket_addr_t addr) {
            rudp.add_connection(addr, 0, make_timespan(10));
            return true;
        });

    auto udp_port = rudp.get_socket()->local_addr().get_port();

    client.on_server_connect([udp_port, this, handler, server](tcp::client_t &c, tcp::connection_t conn) {
        tcp::package_head_t head;
        head.version = 1;
        nat_request_t request;
        request.port = c.get_socket()->local_addr().get_port();
        request.ip = c.get_socket()->local_addr().v4_addr();
        request.udp_port = udp_port;
        request.key = key;
        socket_buffer_t buffer = socket_buffer_t::from_struct(request);
        buffer.expect().origin_length();
        endian::cast_inplace(request, buffer);
        co::await(tcp::conn_awrite_packet, conn, head, buffer);

        /// wait for close
        co::await(tcp::conn_aread_packet_head, conn, head); // the peer closed and it will return io_result::close

        rudp.add_connection(server, 0, make_timespan(10)); // send rudp
    });

    client.connect(ctx, server, make_timespan(10));
}

} // namespace net
