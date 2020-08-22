#include "net/socket.hpp"

namespace net
{
socket_t::socket_t(int fd)
    : fd(fd)
    , is_connection_closed(true)
{
}

socket_t::~socket_t()
{
#ifndef OS_WINDOWS
    close(fd);
#else
    closesocket(fd);
#endif
}

int GetErr()
{
#ifdef OS_WINDOWS
    return WSAGetLastError();
#else
    return errno;
#endif
}

io_result socket_t::write_async(socket_buffer_t &buffer)
{
    while (buffer.get_length() > 0)
    {
        unsigned long buffer_size = buffer.get_length();
        byte *buf = buffer.get();
        int flag = 0;
#ifndef OS_WINDOWS
        flag = MSG_DONTWAIT;
#endif
        auto len = send(fd, (char *)buf, buffer_size, flag);

        int e = GetErr();
        if (len == 0)
        {
            return io_result::cont;
        }
        else if (len < 0)
        {
            if (e == EINTR)
            {
                len = 0;
            }
            else if (e == EPIPE)
            {
                buffer.finish_walk();
                return io_result::closed; // EOF PIPE
            }
            else if (e == EAGAIN)
            {
                return io_result::cont;
            }
            else if (e == ECONNREFUSED)
            {
                throw net_connect_exception("recv message failed!", connection_state::connection_refuse);
            }
            else if (e == ECONNRESET)
            {
                throw net_connect_exception("recv message failed!", connection_state::close_by_peer);
            }
            else
            {
                throw net_io_exception("send message failed!");
            }
        }
        buffer.walk_step(len);
    }
    buffer.finish_walk();
    return io_result::ok;
}

io_result socket_t::read_async(socket_buffer_t &buffer)
{
    size_t len;
    while (buffer.get_length() > 0)
    {
        unsigned long buffer_size = buffer.get_length();
        byte *buf = buffer.get();
        int flag = 0;
#ifndef OS_WINDOWS
        flag = MSG_DONTWAIT;
#endif
        len = recv(fd, (char *)buf, buffer_size, flag);

        if (len == 0) // EOF
        {
            buffer.finish_walk();
            return io_result::closed;
        }
        else if (len < 0)
        {
            int e = GetErr();
            if (e == EINTR)
            {
                len = 0;
            }
            else if (e == EAGAIN)
            {
                // can't read any data
                return io_result::cont;
            }
            else if (e == ECONNREFUSED)
            {
                throw net_connect_exception("recv message failed!", connection_state::connection_refuse);
            }
            else if (e == ECONNRESET)
            {
                throw net_connect_exception("recv message failed!", connection_state::close_by_peer);
            }
            else
            {
                throw net_io_exception("recv message failed!");
            }
        }
        buffer.walk_step(len);
    }
    buffer.finish_walk();
    return io_result::ok;
}

co::async_result_t<io_result> socket_t::awrite(co::paramter_t &param, socket_buffer_t &buffer)
{
    if (param.is_stop())
    {
        if (param.get_times() > 0)
            remove_event(event_type::writable);
        return io_result::timeout;
    }

    if (is_connection_closed)
    {
        throw net_connect_exception("socket closed by peer", connection_state::closed);
    }
    auto ret = write_async(buffer);
    if (ret == io_result::cont)
    {
        if (param.get_times() == 0)
            add_event(event_type::writable);
        return {};
    }
    else if (ret == io_result::closed)
    {
        is_connection_closed = true;
    }
    if (param.get_times() > 0)
        remove_event(event_type::writable);

    return ret;
}

co::async_result_t<io_result> socket_t::aread(co::paramter_t &param, socket_buffer_t &buffer)
{
    if (param.is_stop())
    {
        if (param.get_times() > 0)
            remove_event(event_type::readable);
        return io_result::timeout;
    }

    if (is_connection_closed)
    {
        throw net_connect_exception("socket closed by peer", connection_state::closed);
    }
    auto ret = read_async(buffer);
    if (ret == io_result::cont)
    {
        if (param.get_times() == 0)
            add_event(event_type::readable);
        return {};
    }
    else if (ret == io_result::closed)
    {
        is_connection_closed = true;
    }
    if (param.get_times() > 0)
        remove_event(event_type::readable);
    return ret;
}

io_result socket_t::write_pack(socket_buffer_t &buffer, socket_addr_t target)
{
    auto addr = target.get_raw_addr();
    int flag = 0;
#ifndef OS_WINDOWS
    flag = MSG_DONTWAIT;
#endif
    auto len = sendto(fd, (char *)buffer.get(), buffer.get_length(), flag, (sockaddr *)&addr, (socklen_t)sizeof(addr));
    if (len == 0)
    {
        return io_result::closed;
    }
    else if (len == -1)
    {
        int e = GetErr();

        if (e == EAGAIN)
        {
            return io_result::cont;
        }
        else if (e == EACCES)
        {
            throw net_io_exception("error send to " + target.to_string() + ". permission denied.");
        }
        else if (e == EPIPE)
        {
            return io_result::closed;
        }
        return io_result::failed;
    }
    buffer.walk_step(len);
    buffer.finish_walk();
    return io_result::ok;
}

io_result socket_t::read_pack(socket_buffer_t &buffer, socket_addr_t &target)
{
    auto addr = target.get_raw_addr();
    int flag = 0;
#ifndef OS_WINDOWS
    flag = MSG_DONTWAIT;
#endif
    socklen_t slen = sizeof(addr);
    auto len = recvfrom(fd, (char *)buffer.get(), buffer.get_length(), flag, (sockaddr *)&addr, &slen);
    if (len < 0)
    {
        int e = GetErr();
        if (e == EINTR)
        {
            len = 0;
        }
        if (e == EAGAIN)
        {
            return io_result::cont;
        }
        else if (e == EPIPE)
        {
            return io_result::closed;
        }
        return io_result::failed;
    }
    buffer.walk_step(len);
    buffer.finish_walk();
    target = addr;
    return io_result::ok;
}

co::async_result_t<io_result> socket_t::awrite_to(co::paramter_t &param, socket_buffer_t &buffer, socket_addr_t target)
{
    if (param.is_stop())
    {
        if (param.get_times() > 0)
            remove_event(event_type::writable);
        return io_result::timeout;
    }
    auto ret = write_pack(buffer, target);
    if (ret == io_result::cont)
    {
        if (param.get_times() == 0)
            add_event(event_type::writable);
        return co::async_result_t<io_result>();
    }
    if (param.get_times() > 0)
        remove_event(event_type::writable);
    return ret;
}

co::async_result_t<io_result> socket_t::aread_from(co::paramter_t &param, socket_buffer_t &buffer,
                                                   socket_addr_t &target)
{
    if (param.is_stop())
    {
        if (param.get_times() > 0)
            remove_event(event_type::readable);
        return io_result::timeout;
    }
    auto ret = read_pack(buffer, target);
    if (ret == io_result::cont)
    {
        if (param.get_times() == 0)
            add_event(event_type::readable);
        return co::async_result_t<io_result>();
    }
    if (param.get_times() > 0)
        remove_event(event_type::readable);
    return ret;
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
    if (type & event_type::readable || type & event_type::writable || type & event_type::error)
    {
        start();
    }
    // may be destoried here
}

void socket_t::bind_context(event_context_t &context)
{
    auto &loop = context.select_loop();
    set_loop(&loop);
    loop.add_event_handler(fd, this);
}

void socket_t::unbind_context() { get_loop()->remove_event_handler(fd, this); }

void socket_t::add_event(event_type_t type) { get_loop()->link(fd, type); }

void socket_t::remove_event(event_type_t type) { get_loop()->unlink(fd, type); }

co::async_result_t<io_result> socket_awrite(co::paramter_t &param, socket_t *socket, socket_buffer_t &buffer)
{
    return socket->awrite(param, buffer);
}

co::async_result_t<io_result> socket_aread(co::paramter_t &param, socket_t *socket, socket_buffer_t &buffer)
{
    // async read wrapper
    return socket->aread(param, buffer);
}

co::async_result_t<io_result> socket_awrite_to(co::paramter_t &param, socket_t *socket, socket_buffer_t &buffer,
                                               socket_addr_t target)
{
    return socket->awrite_to(param, buffer, target);
}
co::async_result_t<io_result> socket_aread_from(co::paramter_t &param, socket_t *socket, socket_buffer_t &buffer,
                                                socket_addr_t &target)
{
    return socket->aread_from(param, buffer, target);
}

socket_t *new_tcp_socket()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        throw net_connect_exception("failed to start socket.", connection_state::no_resource);
#ifndef OS_WINDOWS
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    uint64_t flags = 1;
    if (ioctlsocket(fd, FIONBIO, (u_long *)&flags) == SOCKET_ERROR)
    {
        throw net_connect_exception("fail to set socket opt", connection_state::no_resource);
    }
#endif
    auto socket = new socket_t(fd);
    return socket;
}

socket_t *new_udp_socket()
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        throw net_connect_exception("failed to start socket.", connection_state::no_resource);
#ifndef OS_WINDOWS
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    uint64_t flags = 1;
    if (ioctlsocket(fd, FIONBIO, (u_long *)&flags) == SOCKET_ERROR)
    {
        throw net_connect_exception("fail to set socket opt", connection_state::no_resource);
    }
#endif
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
#ifndef OS_WINDOWS
    int opt = reuse;
    setsockopt(socket->get_raw_handle(), SOL_SOCKET, SO_REUSEPORT, (char *)&opt, sizeof(opt));
    return socket;
#else
    return reuse_addr_socket(socket, reuse);
#endif
}

co::async_result_t<io_result> connect_to(co::paramter_t &param, socket_t *socket, socket_addr_t socket_to_addr)
{
    if (param.is_stop())
    {
        socket->remove_event(event_type::readable | event_type::writable);
        return io_result::timeout;
    }

    socklen_t len = sizeof(sockaddr_in);
    auto addr = socket_to_addr.get_raw_addr();

    if (connect(socket->get_raw_handle(), (sockaddr *)&addr, len) == 0)
    {
        socket->is_connection_closed = false;
        socket->remove_event(event_type::readable | event_type::writable);
        return co::async_result_t<io_result>(io_result::ok);
    }
    int e = GetErr();
    if (e == EINPROGRESS)
    {
        socket->add_event(event_type::readable | event_type::writable);
        return co::async_result_t<io_result>();
    }
    else if (e == EISCONN)
    {
        socket->remove_event(event_type::readable | event_type::writable);
        return co::async_result_t<io_result>(io_result::ok);
    }
    socket->remove_event(event_type::readable | event_type::writable);
    socklen_t len2 = sizeof(sockaddr_in);
    sockaddr_in addr2;
    if (getpeername(socket->get_raw_handle(), (sockaddr *)&addr2, &len2) < 0)
    {
        return co::async_result_t<io_result>(io_result::failed);
    }
    socket->is_connection_closed = false;
    return co::async_result_t<io_result>(io_result::ok);
}

co::async_result_t<io_result> connect_udp(co::paramter_t &param, socket_t *socket, socket_addr_t socket_to_addr)
{
    if (param.is_stop())
    {
        socket->remove_event(event_type::readable | event_type::writable);
        return io_result::timeout;
    }

    socklen_t len = sizeof(sockaddr_in);
    auto addr = socket_to_addr.get_raw_addr();

    if (connect(socket->get_raw_handle(), (sockaddr *)&addr, len) == 0)
    {
        socket->remove_event(event_type::readable | event_type::writable);
        return co::async_result_t<io_result>(io_result::ok);
    }
    int e = GetErr();
    if (e == EINPROGRESS)
    {
        socket->add_event(event_type::readable | event_type::writable);
        return co::async_result_t<io_result>();
    }
    else if (e == EISCONN)
    {
        socket->remove_event(event_type::readable | event_type::writable);
        return co::async_result_t<io_result>(io_result::ok);
    }
    socket->remove_event(event_type::readable | event_type::writable);
    socklen_t len2 = sizeof(sockaddr_in);
    sockaddr_in addr2;
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

socket_t *listen_from(socket_t *socket, int max_wait_client)
{
    if (listen(socket->get_raw_handle(), max_wait_client) != 0)
    {
        throw net_connect_exception("failed to listen server.", connection_state::no_resource);
    }
    return socket;
}

co::async_result_t<socket_t *> accept_from(co::paramter_t &param, socket_t *socket)
{
    if (param.is_stop())
    {
        socket->remove_event(event_type::readable);
        return nullptr;
    }

    int fd = accept(socket->get_raw_handle(), 0, 0);
    if (fd < 0)
    {
        int r = GetErr();
        if (r == EAGAIN
#ifdef OS_WINDOWS
            || r == WSAEWOULDBLOCK
#endif
        )
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

#ifndef OS_WINDOWS
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    uint64_t flags = 1;
    if (ioctlsocket(fd, FIONBIO, (u_long *)&flags) == SOCKET_ERROR)
    {
        throw net_connect_exception("fail to set socket opt", connection_state::no_resource);
    }
#endif
    auto socket2 = new socket_t(fd);
    socket2->is_connection_closed = false;
    return socket2;
}

void close_socket(socket_t *socket) { delete socket; }

socket_t *set_socket_send_buffer_size(socket_t *socket, int size)
{
    setsockopt(socket->get_raw_handle(), SOL_SOCKET, SO_SNDBUF, (char *)&size, sizeof(size));
    return socket;
}

socket_t *set_socket_recv_buffer_size(socket_t *socket, int size)
{
    setsockopt(socket->get_raw_handle(), SOL_SOCKET, SO_RCVBUF, (char *)&size, sizeof(size));
    return socket;
}

int get_socket_send_buffer_size(socket_t *socket)
{
    int size;
    socklen_t len = sizeof(size);
    getsockopt(socket->get_raw_handle(), SOL_SOCKET, SO_SNDBUF, (char *)&size, &len);
    return size;
}

int get_socket_recv_buffer_size(socket_t *socket)
{
    int size;
    socklen_t len = sizeof(size);
    getsockopt(socket->get_raw_handle(), SOL_SOCKET, SO_RCVBUF, (char *)&size, &len);
    return size;
}

socket_addr_t get_ip(socket_t *socket)
{
#ifndef OS_WINDOWS
    struct ifreq ifr;

    if (ioctl(socket->get_raw_handle(), SIOCGIFADDR, &ifr) < 0)
    {
        return {};
    }
    return socket_addr_t((u32)(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr).s_addr, 0);
#else
    return {};
#endif
}

} // namespace net
