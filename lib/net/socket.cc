#include "net/socket.hpp"
#include <fcntl.h>

namespace net
{
socket_t::socket_t(int fd)
    : fd(fd)
{
}

socket_t::~socket_t() { close(fd); }

io_result socket_t::write_async(socket_buffer_t &buffer)
{
    unsigned long buffer_offset = 0;
    unsigned long buffer_size = buffer.get_data_len();
    if (buffer_size == 0)
        return io_result::ok;
    byte *buf = buffer.get();
    while (buffer_size > 0)
    {
        auto len = send(fd, buf + buffer_offset, buffer_size, MSG_DONTWAIT);

        if (len == 0)
        {
            // send blocked
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
    return io_result::ok;
}

io_result socket_t::read_async(socket_buffer_t &buffer)
{
    unsigned long buffer_offset = 0;
    unsigned long buffer_size = buffer.get_data_len();
    byte *buf = buffer.get();
    if (buffer_size == 0)
        return io_result::ok;
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
                buffer.set_data_len(buffer_offset);
                // can't read any data
                return io_result::cont;
            }
            else if (errno == ECONNREFUSED)
            {
                throw net_connect_exception("recv message failed!");
            }
            else
            {
                throw net_io_exception("recv message failed!");
            }
        }
        buffer_size -= len;
        buffer_offset += len;
    }
    buffer.set_data_len(buffer_offset);
    return io_result::ok;
}

void socket_t::awrite(socket_buffer_t &buffer) {}

void socket_t::aread(socket_buffer_t &buffer) {}

io_result socket_t::write_pack(socket_buffer_t &buffer, socket_addr_t target)
{
    auto addr = target.get_raw_addr();
    while (1)
    {
        if (sendto(fd, buffer.get(), buffer.get_data_len(), 0, (sockaddr *)&addr, (socklen_t)sizeof(addr)) == -1)
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
        if (recvfrom(fd, buffer.get(), buffer.get_data_len(), 0, (sockaddr *)&addr, &len) == -1)
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
        buffer.set_data_len(len);
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

void socket_t::add_handler(event_handler_t handler) { event_handles.push_back(handler); }

void socket_t::on_event(event_context_t &context, event_type_t type)
{
    for (auto it = event_handles.begin(); it != event_handles.end();)
    {
        auto ret = (*it)(context, type, this);

        if (ret == event_result::remove_handler)
        {
            it = event_handles.erase(it);
        }
        else
        {
            it++;
        }
    }
}

socket_t *connect_to(socket_addr_t socket_to)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        throw net_connect_exception("failed to start socket.");
    socklen_t len = sizeof(sockaddr_in);
    auto addr = socket_to.get_raw_addr();
    connect(fd, (sockaddr *)&addr, len);

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return new socket_t(fd);
}

socket_t *listen_from(socket_addr_t socket_in, int max_count)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        throw net_connect_exception("failed to start socket.");

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

    socklen_t len = sizeof(sockaddr_in);
    auto addr = socket_in.get_raw_addr();
    if (bind(fd, (sockaddr *)&addr, len) != 0)
        throw net_connect_exception("failed to bind address " + socket_in.to_string());
    if (listen(fd, max_count) != 0)
        throw net_connect_exception("failed to listen address " + socket_in.to_string());

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    return new socket_t(fd);
}

socket_t *accept_from(socket_t *socket)
{
    while (1)
    {
        int fd = accept(socket->get_raw_handle(), 0, 0);
        if (fd < 0)
        {
            if (errno == EAGAIN)
            {
                // wait

                continue;
            }
            throw net_connect_exception("failed to accept " + socket->local_addr().to_string());
        }
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        return new socket_t(fd);
    };
}

void close_socket(socket_t *socket) { delete socket; }
} // namespace net
