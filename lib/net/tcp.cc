#include "net/tcp.hpp"
#include "net/socket.hpp"
#include <functional>
#include <iostream>

namespace net::tcp
{

co::async_result_t<io_result> connection_t::awrite(co::paramter_t &param, socket_buffer_t &buffer)
{
    return socket_awrite(param, socket, buffer);
}

co::async_result_t<io_result> connection_t::aread(co::paramter_t &param, socket_buffer_t &buffer)
{
    return socket_aread(param, socket, buffer);
}

/// wait next packet and read tcp application head
co::async_result_t<io_result> connection_t::aread_packet_head(co::paramter_t &param, package_head_t &head)
{
    /// XXX: there is a memory allocate in head.
    if (param.get_user_ptr() == 0) /// version none, first read
    {
        socket_buffer_t buffer((byte *)&head, sizeof(package_head_t));
        buffer.expect().length(sizeof(head.version));

        auto res = socket_aread(param, socket, buffer);
        if (!res.is_finish())
        {
            return {};
        }
        if (res() != io_result::ok)
            return res();
        if (head.version > 4 || head.version < 1) /// unknown header version
            return io_result::failed;

        /// save state, and we don't execute this branch any more.
        param.set_user_ptr((void *)1);
    }
    // read head

    // buffer in stack
    byte rest_buffer[sizeof(package_head_t)];

    socket_buffer_t buffer(rest_buffer, sizeof(rest_buffer));
    int length;
    assert(head.version <= 4 && head.version >= 1);
    switch (head.version)
    {
        case 1:
            length = sizeof(head.v1);
            break;
        case 2:
            length = sizeof(head.v2);
            break;
        case 3:
            length = sizeof(head.v3);
            break;
        case 4:
            length = sizeof(head.v4);
            break;
        default:
            break;
    }
    buffer.expect().length(length);                  /// set size we expect
    buffer.set_process_length(sizeof(head.version)); /// save to buffer at offset 1.

    auto res = socket_aread(param, socket, buffer);
    if (res.is_finish())
    {
        if (res() == io_result::ok)
        {
            buffer.get_raw_ptr()[0] = head.version;
            buffer.expect().origin_length();
            switch (head.version)
            {
                case 1:
                    assert(endian::cast_to(buffer, head.v1));
                    break;
                case 2:
                    assert(endian::cast_to(buffer, head.v2));
                    break;
                case 3:
                    assert(endian::cast_to(buffer, head.v3));
                    break;
                case 4:
                    assert(endian::cast_to(buffer, head.v4));
                    break;
                default:
                    break;
            }
        }
    }
    return res;
}

co::async_result_t<io_result> connection_t::aread_packet_content(co::paramter_t &param, socket_buffer_t &buffer)
{
    return socket_aread(param, socket, buffer);
}

template <typename E>
co::async_result_t<io_result> set_head_and_send(co::paramter_t &param, E &head, u64 buf_size, socket_t *socket)
{
    int head_buffer_size = sizeof(E);
    head.size = buf_size;
    socket_buffer_t head_buffer(head_buffer_size);

    head_buffer.expect().origin_length();
    assert(endian::save_to(head, head_buffer));
    auto ret = socket_awrite(param, socket, head_buffer);
    if (ret.is_finish())
    {
        if (ret() == io_result::ok)
        {
            param.set_user_ptr((void *)1);
            return ret;
        }
    }
    return ret;
}

co::async_result_t<io_result> connection_t::awrite_packet(co::paramter_t &param, package_head_t &head,
                                                          socket_buffer_t &buffer)
{
send_head:
    if (param.get_user_ptr() == 0)
    {
        int head_buffer_size;
        switch (head.version)
        {
            case 1: {

                auto ret = set_head_and_send(param, head.v1, buffer.get_data_length(), socket);
                if (ret.is_finish())
                {
                    if (ret() != io_result::ok)
                        return ret;
                    break;
                }
            }
                return {};
            case 2: {
                auto ret = set_head_and_send(param, head.v2, buffer.get_data_length(), socket);
                if (ret.is_finish())
                {
                    if (ret() != io_result::ok)
                        return ret;
                    break;
                }
            }
                return {};
            case 3: {
                auto ret = set_head_and_send(param, head.v3, buffer.get_data_length(), socket);
                if (ret.is_finish())
                {
                    if (ret() != io_result::ok)
                        return ret;
                    break;
                }
            }
                return {};
            case 4: {
                auto ret = set_head_and_send(param, head.v4, buffer.get_data_length(), socket);
                if (ret.is_finish())
                {
                    if (ret() != io_result::ok)
                        return ret;
                    break;
                }
            }
                return {};
            default:
                return io_result::failed;
        }
    }

send_data:

    return socket_awrite(param, socket, buffer);
}

co::async_result_t<io_result> conn_awrite(co::paramter_t &param, connection_t conn, socket_buffer_t &buffer)
{
    return conn.awrite(param, buffer);
}
co::async_result_t<io_result> conn_aread(co::paramter_t &param, connection_t conn, socket_buffer_t &buffer)
{
    return conn.aread(param, buffer);
}
co::async_result_t<io_result> conn_aread_packet_head(co::paramter_t &param, connection_t conn, package_head_t &head)
{
    return conn.aread_packet_head(param, head);
}
co::async_result_t<io_result> conn_aread_packet_content(co::paramter_t &param, connection_t conn,
                                                        socket_buffer_t &buffer)
{
    return conn.aread_packet_content(param, buffer);
}
co::async_result_t<io_result> conn_awrite_packet(co::paramter_t &param, connection_t conn, package_head_t &head,
                                                 socket_buffer_t &buffer)
{
    return conn.awrite_packet(param, head, buffer);
}

server_t::server_t()
    : server_socket(nullptr)
{
}

server_t::~server_t() { close_server(); }

void server_t::client_main(socket_t *socket)
{
    context->add_socket(socket);
    try
    {
        if (join_handler)
            join_handler(*this, socket);
    } catch (net::net_connect_exception &e)
    {
        if (e.get_state() != connection_state::closed)
        {
            if (error_handler)
                error_handler(*this, socket, e.get_state());
        }
    }
    exit_client(socket);
}

void server_t::wait_client()
{
    while (1)
    {
        auto socket = co::await(accept_from, server_socket);
        auto co = co::coroutine_t::create(std::bind(&server_t::client_main, this, socket));
        socket->startup_coroutine(co);
    }
}

void server_t::listen(event_context_t &context, socket_addr_t address, int max_client, bool reuse_addr)
{
    this->context = &context;
    server_socket = new_tcp_socket();
    if (reuse_addr)
        reuse_addr_socket(server_socket, true);

    listen_from(bind_at(server_socket, address), max_client);

    context.add_socket(server_socket).link(server_socket, net::event_type::readable);
    auto cot = co::coroutine_t::create(std::bind(&server_t::wait_client, this));
    server_socket->startup_coroutine(cot);
}

void server_t::exit_client(socket_t *client)
{
    assert(client != server_socket);

    if (!client)
        return;
    if (exit_handler)
        exit_handler(*this, client);

    context->remove_socket(client);
    if (co::coroutine_t::in_coroutine(client->get_coroutine()) && client->get_coroutine() != nullptr)
    {
        co::coroutine_t::yield([client]() { close_socket(client); });
    }
    else
    {
        close_socket(client);
    }
}

void server_t::close_server()
{
    if (!server_socket)
        return;

    context->remove_socket(server_socket);
    if (co::coroutine_t::in_coroutine(server_socket->get_coroutine()) && server_socket->get_coroutine() != nullptr)
    {
        co::coroutine_t::yield([this]() {
            close_socket(server_socket);
            server_socket = nullptr;
        });
    }
    else
    {
        close_socket(server_socket);
        server_socket = nullptr;
    }
}

server_t &server_t::on_client_join(handler_t handler)
{
    join_handler = handler;
    return *this;
}

server_t &server_t::on_client_exit(handler_t handler)
{
    exit_handler = handler;
    return *this;
}

server_t &server_t::on_client_error(error_handler_t handler)
{
    error_handler = handler;
    return *this;
}

client_t::client_t()
    : socket(nullptr)
{
}

client_t::~client_t() { close(); }

void client_t::wait_server(socket_addr_t address, microsecond_t timeout)
{
    auto ret = co::await_timeout(timeout, connect_to, socket, address);
    if (io_result::ok == ret)
    {
        try
        {
            if (join_handler)
                join_handler(*this, socket);
        } catch (net::net_connect_exception &e)
        {
            if (e.get_state() != connection_state::closed)
            {
                if (error_handler)
                    error_handler(*this, socket, e.get_state());
            }
        }
    }
    else
    {
        if (error_handler)
        {
            if (io_result::timeout == ret)
                error_handler(*this, socket, connection_state::timeout);
            else
                error_handler(*this, socket, connection_state::closed);
        }
    }
    close();
}

void client_t::connect(event_context_t &context, socket_addr_t address, microsecond_t timeout)
{
    this->context = &context;
    socket = new_tcp_socket();
    connect_addr = address;

    context.add_socket(socket);
    auto cot = co::coroutine_t::create(std::bind(&client_t::wait_server, this, address, timeout));
    socket->startup_coroutine(cot);
}

client_t &client_t::on_server_connect(handler_t handler)
{
    join_handler = handler;
    return *this;
}

client_t &client_t::on_server_disconnect(handler_t handler)
{
    exit_handler = handler;
    return *this;
}

client_t &client_t::on_server_error(error_handler_t handler)
{
    error_handler = handler;
    return *this;
}

void client_t::close()
{
    if (!socket)
        return;
    if (exit_handler)
        exit_handler(*this, socket);
    context->remove_socket(socket);
    if (co::coroutine_t::in_coroutine(socket->get_coroutine()) && socket->get_coroutine() != nullptr)
    {
        co::coroutine_t::yield([this]() {
            close_socket(socket);
            socket = nullptr;
        });
    }
    else
    {
        close_socket(socket);
        socket = nullptr;
    }
}

} // namespace net::tcp
