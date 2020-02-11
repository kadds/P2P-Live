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
    byte *buf = buffer.get_raw_ptr();
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
                buffer.end_process();
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
    buffer.end_process();
    return io_result::ok;
}

io_result socket_t::read_async(socket_buffer_t &buffer)
{
    unsigned long buffer_offset = buffer.get_process_length();
    unsigned long buffer_size = buffer.get_data_length() - buffer.get_process_length();
    byte *buf = buffer.get_raw_ptr();
    ssize_t len;
    while (buffer_size > 0)
    {
        len = recv(fd, buf + buffer_offset, buffer_size, MSG_DONTWAIT);
        if (len == 0) // EOF
        {
            buffer.end_process();
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
    buffer.end_process();
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
    unsigned long buffer_offset = buffer.get_process_length();
    unsigned long buffer_size = buffer.get_data_length() - buffer.get_process_length();
    auto len = sendto(fd, buffer.get_raw_ptr(), buffer.get_data_length(), MSG_DONTWAIT, (sockaddr *)&addr,
                      (socklen_t)sizeof(addr));
    if (len == 0)
    {
        return io_result::closed;
    }
    else if (len == -1)
    {
        if (errno == EAGAIN)
        {
            return io_result::cont;
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
    buffer.set_process_length(len);
    buffer.end_process();
    return io_result::ok;
}

io_result socket_t::read_pack(socket_buffer_t &buffer, socket_addr_t &target)
{
    auto addr = target.get_raw_addr();
    socklen_t slen = sizeof(addr);
    auto len = recvfrom(fd, buffer.get_raw_ptr(), buffer.get_data_length(), MSG_DONTWAIT, (sockaddr *)&addr, &slen);
    if (len == 0)
    {
        return io_result::closed;
    }
    else if (len < 0)
    {
        if (errno == EINTR)
        {
            len = 0;
        }
        if (errno == EAGAIN)
        {
            return io_result::cont;
        }
        else if (errno == ECONNREFUSED)
        {
            return io_result::closed;
        }
        return io_result::failed;
    }
    buffer.set_process_length(len);
    buffer.end_process();
    target = addr;
    return io_result::ok;
}

co::async_result_t<io_result> socket_t::awrite_to(socket_buffer_t &buffer, socket_addr_t target)
{
    auto ret = write_pack(buffer, target);
    if (ret == io_result::cont)
    {
        add_event(event_type::writable);
        return co::async_result_t<io_result>();
    }
    return ret;
}

co::async_result_t<io_result> socket_t::aread_from(socket_buffer_t &buffer, socket_addr_t &target)
{
    auto ret = read_pack(buffer, target);
    if (ret == io_result::cont)
    {
        add_event(event_type::readable);
        return co::async_result_t<io_result>();
    }
    return ret;
}

void socket_t::sleep(microsecond_t span)
{
    if (loop->add_timer(make_timer(span, [this]() { co->resume(); })) >= 0)
    {
        co::coroutine_t::yield();
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
    if (this->co)
        throw std::logic_error("coroutine has been set.");
    this->co = co;
    co->resume();
}

co::async_result_t<io_result> socket_awrite(socket_t *socket, socket_buffer_t &buffer)
{
    return socket->awrite(buffer);
}

co::async_result_t<io_result> socket_aread(socket_t *socket, socket_buffer_t &buffer)
{
    // async read wrapper
    return socket->aread(buffer);
}

co::async_result_t<io_result> socket_awrite_to(socket_t *socket, socket_buffer_t &buffer, socket_addr_t target)
{
    return socket->awrite_to(buffer, target);
}
co::async_result_t<io_result> socket_aread_from(socket_t *socket, socket_buffer_t &buffer, socket_addr_t &target)
{
    return socket->aread_from(buffer, target);
}

socket_t *new_tcp_socket()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        throw net_connect_exception("failed to start socket.", connection_state::no_resource);
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

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

    auto socket = new socket_t(fd);
    return socket;
}

socket_t *reuse_addr_socket(socket_t *socket, bool reuse)
{
    int opt = reuse;
    setsockopt(socket->get_raw_handle(), SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));
    return socket;
}

socket_t *reuse_port_socket(socket_t *socket, bool reuse)
{
    int opt = reuse;
    setsockopt(socket->get_raw_handle(), SOL_SOCKET, SO_REUSEPORT, (char *)&opt, sizeof(opt));
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

co::async_result_t<io_result> connect_udp(socket_t *socket, socket_addr_t socket_to_addr, int timeout_ms)
{
    socklen_t len = sizeof(sockaddr_in);
    auto addr = socket_to_addr.get_raw_addr();

    if (connect(socket->get_raw_handle(), (sockaddr *)&addr, len) == 0)
    {
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
