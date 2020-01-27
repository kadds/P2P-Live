#include "net/socket.hpp"
namespace net
{
socket_t::socket_t(int fd)
    : fd(fd)
{
}

socket_t::~socket_t() {}

io_result socket_t::write(socket_buffer_t &buffer)
{
    auto len = send(fd, buffer.get(), buffer.get_data_len(), 0);
    if (len == -1)
    {
        return io_result::closed;
    }
    return io_result::ok;
}

io_result socket_t::read(socket_buffer_t &buffer)
{
    auto len = recv(fd, buffer.get(), buffer.get_buffer_len(), 0);
    if (len == -1)
    {
        return io_result::closed;
    }
    else if (len == 0)
    {
        return io_result::closed;
    }
    buffer.set_data_len(len);
    return io_result::ok;
}

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
        return socket_addr_t(in);
    }
    throw net_param_exception("failed to get addr");
}

socket_addr_t socket_t::remote_addr()
{
    sockaddr_in in;
    socklen_t len = sizeof(sockaddr_in);
    if (getpeername(fd, (sockaddr *)&in, &len) == 0)
    {
        return socket_addr_t(in);
    }
    throw net_param_exception("failed to get addr");
}

socket_t connect_to(socket_addr_t socket_to)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    return fd;
}

socket_t listen_from(socket_addr_t socket_in, int max_count)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        throw net_connect_exception("failed to start socket.");

    socklen_t len = sizeof(sockaddr_in);
    auto addr = socket_in.get_raw_addr();
    if (bind(fd, (sockaddr *)&addr, len) != 0)
        throw net_connect_exception("failed to bind address " + socket_in.to_string());
    if (listen(fd, max_count) != 0)
        throw net_connect_exception("failed to listen address " + socket_in.to_string());

    return fd;
}

socket_t accept_from(socket_t socket)
{
    int fd = accept(socket.get_raw_handle(), 0, 0);
    if (fd < 0)
    {
        int r = errno;
        auto str = "failed to accept " + socket.local_addr().to_string();
        throw net_connect_exception(str);
    }

    return fd;
}

void close_socket(socket_t socket) { close(socket.get_raw_handle()); }

} // namespace net
