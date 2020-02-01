#include "net/socket.hpp"
#include <fcntl.h>

namespace net
{
socket_t::socket_t(int fd)
    : fd(fd)
    , current_event(0)
    , is_connection_closed(true)
    , co(nullptr)
{
}

socket_t::~socket_t()
{
    if (co)
        co::coroutine_t::remove(co);
    close(fd);
}

io_result socket_t::write_async(socket_buffer_t &buffer)
{
    unsigned long buffer_offset = buffer.get_process_length();
    unsigned long buffer_size = buffer.get_data_length() - buffer.get_process_length();
    byte *buf = buffer.get();
    while (buffer_size > 0)
    {
        auto len = send(fd, buf + buffer_offset, buffer_size, MSG_DONTWAIT);

        if (len == 0)
        {
            buffer.set_process_length(buffer_offset);
            return io_result::cont;
        }
        else if (len < 0)
        {
            int e = errno;
            if (errno == EINTR)
            {
                len = 0;
            }
            else if (errno == EPIPE)
            {
                return io_result::closed; // EOF PIPE
            }
            else
            {
                throw net_io_exception("send message failed!");
            }
        }

        buffer_size -= len;
        buffer_offset += len;
    }
    buffer.set_process_length(buffer_offset);
    return io_result::ok;
}

io_result socket_t::read_async(socket_buffer_t &buffer)
{
    unsigned long buffer_offset = buffer.get_process_length();
    unsigned long buffer_size = buffer.get_data_length() - buffer.get_process_length();
    byte *buf = buffer.get();
    ssize_t len;
    while (buffer_size > 0)
    {
        len = recv(fd, buf + buffer_offset, buffer_size, MSG_DONTWAIT);
        if (len == 0) // EOF
        {
            return io_result::closed;
        }
        else if (len < 0)
        {
            if (errno == EINTR)
            {
                len = 0;
            }
            else if (errno == EAGAIN)
            {
                buffer.set_process_length(buffer_offset);
                // can't read any data
                return io_result::cont;
            }
            else if (errno == ECONNREFUSED)
            {
                throw net_connect_exception("recv message failed!", connection_state::connection_refuse);
            }
            else
            {
                throw net_io_exception("recv message failed!");
            }
        }
        buffer_size -= len;
        buffer_offset += len;
    }
    buffer.set_process_length(buffer_offset);
    return io_result::ok;
}

co::async_result_t<io_result> socket_t::awrite(socket_buffer_t &buffer)
{
    if (is_connection_closed)
    {
        throw net_connect_exception("socket closed by peer", connection_state::closed);
    }
    auto ret = write_async(buffer);
    if (ret == io_result::cont)
    {
        add_event(event_type::writable);
        return co::async_result_t<io_result>();
    }
    else if (ret == io_result::closed)
    {
        is_connection_closed = true;
    }
    remove_event(event_type::writable);
    return ret;
}

co::async_result_t<io_result> socket_t::aread(socket_buffer_t &buffer)
{
    if (is_connection_closed)
    {
        throw net_connect_exception("socket closed by peer", connection_state::closed);
    }
    auto ret = read_async(buffer);
    if (ret == io_result::cont)
    {
        add_event(event_type::readable);
        return co::async_result_t<io_result>();
    }
    else if (ret == io_result::closed)
    {
        is_connection_closed = true;
    }
    remove_event(event_type::readable);
    return ret;
}

io_result socket_t::write_pack(socket_buffer_t &buffer, socket_addr_t target)
{
    auto addr = target.get_raw_addr();
    while (1)
    {
        if (sendto(fd, buffer.get(), buffer.get_data_length(), 0, (sockaddr *)&addr, (socklen_t)sizeof(addr)) == -1)
        {
            if (errno == EAGAIN)
            {
                continue;
            }
            else if (errno == EACCES)
            {
                throw net_io_exception("error send to " + target.to_string() + ". permission denied.");
            }
            else if (errno == EPIPE)
            {
                return io_result::closed;
            }
            return io_result::failed;
        }
        return io_result::ok;
    }
}

io_result socket_t::read_pack(socket_buffer_t &buffer, socket_addr_t target)
{
    auto addr = target.get_raw_addr();
    socklen_t len = sizeof(addr);
    while (1)
    {
        if (recvfrom(fd, buffer.get(), buffer.get_data_length(), 0, (sockaddr *)&addr, &len) == -1)
        {
            if (errno == EAGAIN)
            {
                continue;
            }
            else if (errno == ECONNREFUSED)
            {
                return io_result::closed;
            }
            return io_result::failed;
        }
        buffer.set_process_length(len);
        return io_result::ok;
    }
}

socket_addr_t socket_t::local_addr()
{
    sockaddr_in in;
    socklen_t len = sizeof(sockaddr_in);
    if (getsockname(fd, (sockaddr *)&in, &len) == 0)
    {
        local = in;
    }
    return local;
}

socket_addr_t socket_t::remote_addr()
{
    sockaddr_in in;
    socklen_t len = sizeof(sockaddr_in);
    if (getpeername(fd, (sockaddr *)&in, &len) == 0)
    {
        remote = in;
    }
    return remote;
}

void socket_t::on_event(event_context_t &context, event_type_t type)
{
    current_event = type;

    if (type & event_type::readable || type & event_type::writable || type & event_type::error)
    {
        co->resume();
    }

    current_event = 0;
}

void socket_t::add_event(event_type_t type)
{
    assert(co::coroutine_t::current() == co);
    loop->link(this, type);
}

void socket_t::remove_event(event_type_t type)
{
    assert(co::coroutine_t::current() == co);
    loop->unlink(this, type);
}

void socket_t::startup_coroutine(co::coroutine_t *co)
{
    this->co = co;
    co->resume();
}

socket_t *new_tcp_socket()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        throw net_connect_exception("failed to start socket.", connection_state::no_resource);
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

    auto socket = new socket_t(fd);
    return socket;
}

socket_t *new_udp_socket()
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        throw net_connect_exception("failed to start socket.", connection_state::no_resource);
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

    auto socket = new socket_t(fd);
    return socket;
}

co::async_result_t<io_result> connect_to(socket_t *socket, socket_addr_t socket_to_addr, int timeout_ms)
{
    socklen_t len = sizeof(sockaddr_in);
    auto addr = socket_to_addr.get_raw_addr();

    if (connect(socket->get_raw_handle(), (sockaddr *)&addr, len) == 0)
    {
        socket->is_connection_closed = false;
        socket->remove_event(event_type::readable | event_type::writable);
        return co::async_result_t<io_result>(io_result::ok);
    }
    if (errno == EINPROGRESS)
    {
        socket->add_event(event_type::readable | event_type::writable);
        return co::async_result_t<io_result>();
    }
    else if (errno == EISCONN)
    {
        socket->remove_event(event_type::readable | event_type::writable);
        return co::async_result_t<io_result>(io_result::ok);
    }
    socket->remove_event(event_type::readable | event_type::writable);
    socklen_t len2 = sizeof(sockaddr_in);
    sockaddr_in addr2;
    int er = errno;
    if (getpeername(socket->get_raw_handle(), (sockaddr *)&addr2, &len2) < 0)
    {
        return co::async_result_t<io_result>(io_result::failed);
    }
    socket->is_connection_closed = false;
    return co::async_result_t<io_result>(io_result::ok);
}

socket_t *bind_at(socket_t *socket, socket_addr_t socket_to_addr)
{
    socklen_t len = sizeof(sockaddr_in);
    auto addr = socket_to_addr.get_raw_addr();
    if (bind(socket->get_raw_handle(), (sockaddr *)&addr, len) != 0)
    {
        throw net_connect_exception("failed to bind address " + socket_to_addr.to_string(),
                                    connection_state::address_in_used);
    }
    return socket;
}

socket_t *listen_from(socket_t *socket, int max_count)
{
    if (listen(socket->get_raw_handle(), max_count) != 0)
    {
        throw net_connect_exception("failed to listen server.", connection_state::no_resource);
    }
    return socket;
}

co::async_result_t<socket_t *> accept_from(socket_t *socket)
{
    int fd = accept(socket->get_raw_handle(), 0, 0);
    if (fd < 0)
    {
        if (errno == EAGAIN)
        {
            // wait
            socket->add_event(event_type::readable);
            return co::async_result_t<socket_t *>();
        }
        socket->remove_event(event_type::readable);

        throw net_connect_exception("failed to accept " + socket->local_addr().to_string(),
                                    connection_state::no_resource);
    }
    socket->remove_event(event_type::readable);

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    auto socket2 = new socket_t(fd);
    socket2->is_connection_closed = false;
    return socket2;
}

void close_socket(socket_t *socket) { delete socket; }

} // namespace net
