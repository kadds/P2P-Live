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

co::async_result_t<io_result> bsd_socket_t::awrite(co::paramter_t &param, socket_buffer_t &buffer)
{
    if (param.is_stop())
    {
        if (param.get_times() > 0)
            remove_event(event_type::writable);
        return io_result::timeout;
    }
    if (is_connection_closed)
        throw net_connect_exception("socket closed by peer", connection_state::closed);

    io_result ret = io_result::ok;

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
                // EOF PIPE
                ret = io_result::closed;
                break;
            }
            else if (e == WOULDBLOCK)
            {
                ret = io_result::cont;
                break;
            }
            else if (e == ECONNREFUSED)
                throw net_connect_exception("recv message failed!", connection_state::connection_refuse);
            else if (e == ECONNRESET)
                throw net_connect_exception("recv message failed!", connection_state::close_by_peer);
            else
                throw net_io_exception("send message failed!");
        }
        buffer.walk_step(len);
    }
    if (ret == io_result::cont)
    {
        if (param.get_times() == 0)
            add_event(event_type::writable);
        return {};
    }
    if (ret == io_result::closed)
        is_connection_closed = true;

    if (param.get_times() > 0)
        remove_event(event_type::writable);

    buffer.finish_walk();
    return ret;
}

co::async_result_t<io_result> bsd_socket_t::aread(co::paramter_t &param, socket_buffer_t &buffer)
{
    if (param.is_stop())
    {
        if (param.get_times() > 0)
            remove_event(event_type::readable);
        return io_result::timeout;
    }

    if (is_connection_closed)
        throw net_connect_exception("socket closed by peer", connection_state::closed);

    long long len;
    io_result ret = io_result::ok;
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
            ret = io_result::closed;
            break;
        }
        else if (len < 0)
        {
            int e = GetErr();
            if (e == EINTR)
            {
                len = 0;
            }
            else if (e == WOULDBLOCK)
            {
                // can't read any data
                ret = io_result::cont;
                break;
            }
            else if (e == ECONNREFUSED)
                throw net_connect_exception("recv message failed!", connection_state::connection_refuse);
            else if (e == ECONNRESET)
                throw net_connect_exception("recv message failed!", connection_state::close_by_peer);
            else
                throw net_io_exception("recv message failed!");
        }
        buffer.walk_step(len);
    }
    if (ret == io_result::cont)
    {
        if (param.get_times() == 0)
            add_event(event_type::readable);
        return {};
    }

    if (ret == io_result::closed)
        is_connection_closed = true;

    if (param.get_times() > 0)
        remove_event(event_type::readable);

    buffer.finish_walk();
    return ret;
}

co::async_result_t<io_result> bsd_socket_t::awrite_to(co::paramter_t &param, socket_buffer_t &buffer,
                                                      socket_addr_t target)
{
    if (param.is_stop())
    {
        if (param.get_times() > 0)
            remove_event(event_type::writable);
        return io_result::timeout;
    }
    io_result ret = io_result::ok;
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
    else if (len < 0)
    {
        int e = GetErr();
        if (e == WOULDBLOCK)
            ret = io_result::cont;
        else if (e == EACCES)
            throw net_io_exception("error send to " + target.to_string() + ". permission denied.");
        else if (e == EPIPE)
            ret = io_result::closed;
        else
            ret = io_result::failed;
    }
    if (ret == io_result::cont)
    {
        if (param.get_times() == 0)
            add_event(event_type::writable);
        return {};
    }

    if (param.get_times() > 0)
        remove_event(event_type::writable);

    buffer.walk_step(len);
    buffer.finish_walk();
    return ret;
}

co::async_result_t<io_result> bsd_socket_t::aread_from(co::paramter_t &param, socket_buffer_t &buffer,
                                                       socket_addr_t &target)
{
    if (param.is_stop())
    {
        if (param.get_times() > 0)
            remove_event(event_type::readable);
        return io_result::timeout;
    }
    io_result ret = io_result::ok;
    auto addr = target.get_raw_addr();
    int flag = 0;
#ifndef OS_WINDOWS
    flag = MSG_DONTWAIT;
#endif
    socklen_t slen = sizeof(addr);
    auto len = recvfrom(fd, (char *)buffer.get(), buffer.get_length(), flag, (sockaddr *)&addr, &slen);
    if (len == 0)
    {
        return io_result::closed;
    }
    if (len < 0)
    {
        int e = GetErr();
        if (e == EINTR)
            len = 0;
        if (e == WOULDBLOCK)
            ret = io_result::cont;
        else if (e == EPIPE)
            ret = io_result::closed;
        else
            ret = io_result::failed;
    }
    if (ret == io_result::cont)
    {
        if (param.get_times() == 0)
            add_event(event_type::readable);
        return {};
    }

    if (param.get_times() > 0)
        remove_event(event_type::readable);

    buffer.walk_step(len);
    buffer.finish_walk();

    target = addr;
    return ret;
}

#ifdef OS_WINDOWS
co::async_result_t<io_result> iocp_socket_t::awrite(co::paramter_t &param, socket_buffer_t &buffer)
{
    auto io = (io_overlapped *)param.get_user_ptr();
    io_result ret = io_result::ok;
    bool need_send = false;
    if (param.get_times() == 0)
    {
        if (is_connection_closed)
            throw net_connect_exception("socket closed by peer", connection_state::closed);

        void *buf = buffer.get();
        io = new io_overlapped();
        io->wsaBuf.buf = (CHAR *)buf;
        io->wsaBuf.len = buffer.get_length();
        io->sock = (HANDLE)fd;
        io->type = io_type::write;
        param.set_user_ptr(io);
        need_send = true;
    }
    while (1)
    {
        if (need_send)
        {
            DWORD dwWrite = 0;
            int err = WSASend(fd, (LPWSABUF)&io->wsaBuf, 1, &dwWrite, 0, &io->overlapped, 0);

            if (err == SOCKET_ERROR)
            {
                err = GetErr();
                if (err == ERROR_IO_PENDING)
                    return {};
                else if (err == WSAEDISCON || err == WSAENOTCONN)
                    ret = io_result::closed;
                else
                    throw net_io_exception("awrite fail");
            }
            buffer.walk_step(dwWrite);
        }
        else
        {
            if (io->buffer_do_len > buffer.get_length())
            {
                throw net_io_exception("fail buffer size");
            }
            buffer.walk_step(io->buffer_do_len);
            if (io->buffer_do_len == 0 && io->done)
            {
                ret = io_result::closed;
            }
            if (io->err != 0)
            {
                ret = io_result::failed;
            }
            io->buffer_do_len = 0;
        }

        if (buffer.get_walk_offset() >= buffer.get_data_length())
        {
            buffer.finish_walk();
            break;
        }
        else
        {
            if (ret != io_result::ok)
            {
                break;
            }
            io->wsaBuf.buf = (CHAR *)buffer.get();
            io->wsaBuf.len = buffer.get_length();
            ZeroMemory(&io->overlapped, sizeof(io->overlapped));
            need_send = true;
        }
    }
    if (param.is_stop())
    {
        BOOL ok = CancelIoEx((HANDLE)fd, &io->overlapped);
        if (ok == FALSE)
        {
            throw net_io_exception("cancel fail");
        }
        delete io;
        return io_result::timeout;
    }
    delete io;
    return ret;
}

co::async_result_t<io_result> iocp_socket_t::aread(co::paramter_t &param, socket_buffer_t &buffer)
{
    auto io = (io_overlapped *)param.get_user_ptr();
    io_result ret = io_result::ok;
    bool need_recv = false;
    if (param.get_times() == 0)
    {
        if (is_connection_closed)
            throw net_connect_exception("socket closed by peer", connection_state::closed);

        io = new io_overlapped();
        io->wsaBuf.buf = (CHAR *)buffer.get();
        io->wsaBuf.len = buffer.get_length();
        io->sock = (HANDLE)fd;
        io->type = io_type::read;
        param.set_user_ptr(io);
        need_recv = true;
    }

    while (1)
    {
        DWORD dwRead = 0;
        DWORD flag = 0;

        if (need_recv)
        {
            int err = WSARecv(fd, (LPWSABUF)&io->wsaBuf, 1, &dwRead, &flag, &io->overlapped, 0);
            if (err == SOCKET_ERROR)
            {
                err = GetErr();
                if (err == ERROR_IO_PENDING)
                    return {};
                else if (err == WSAEDISCON || err == WSAENOTCONN)
                    ret = io_result::closed;
                else
                    throw net_io_exception("aread fail");
            }
            if (ret == io_result::ok)
                return {};
        }
        else
        {
            if (io->buffer_do_len > buffer.get_length())
            {
                throw net_io_exception("fail buffer size");
            }
            buffer.walk_step(io->buffer_do_len);
            if (io->buffer_do_len == 0 && io->done)
            {
                ret = io_result::closed;
            }
            if (io->err != 0)
            {
                ret = io_result::failed;
            }
            io->buffer_do_len = 0;
        }
        if (buffer.get_walk_offset() >= buffer.get_data_length())
        {
            buffer.finish_walk();
            break;
        }
        if (ret != io_result::ok)
        {
            break;
        }
        else
        {
            // cont
            io->wsaBuf.buf = (CHAR *)buffer.get();
            io->wsaBuf.len = buffer.get_length();
            ZeroMemory(&io->overlapped, sizeof(OVERLAPPED));
            need_recv = true;
        }
    }
    if (param.is_stop())
    {
        BOOL ok = CancelIoEx((HANDLE)fd, &io->overlapped);
        if (ok == FALSE)
        {
            throw net_io_exception("cancel fail");
        }
        delete io;
        return io_result::timeout;
    }
    delete io;
    return ret;
}

co::async_result_t<io_result> iocp_socket_t::awrite_to(co::paramter_t &param, socket_buffer_t &buffer,
                                                       socket_addr_t target)
{
    auto io = (io_overlapped *)param.get_user_ptr();
    if (param.get_times() == 0)
    {
        if (is_connection_closed)
            throw net_connect_exception("socket closed by peer", connection_state::closed);
        void *buf = buffer.get();
        io = new io_overlapped();
        io->wsaBuf.buf = (CHAR *)buf;
        io->wsaBuf.len = buffer.get_length();
        io->addr = target.get_raw_addr();
        io->addr_len = sizeof(io->addr);
        io->sock = (HANDLE)fd;
        param.set_user_ptr(io);
        DWORD len = 0;
        int err =
            WSASendTo(fd, (LPWSABUF)&io->wsaBuf, 1, &len, 0, (SOCKADDR *)&io->addr, io->addr_len, &io->overlapped, 0);
        if (err == SOCKET_ERROR)
        {
            err = GetErr();
            if (err == ERROR_IO_PENDING)
                return {};
            else
                throw net_io_exception("awrite to fail");
        }
    }

    delete io;
    if (param.is_stop())
        return io_result::timeout;
    buffer.walk_step(buffer.get_length());
    buffer.finish_walk();
    return io_result::ok;
}

co::async_result_t<io_result> iocp_socket_t::aread_from(co::paramter_t &param, socket_buffer_t &buffer,
                                                        socket_addr_t &target)
{
    auto io = (io_overlapped *)param.get_user_ptr();
    if (param.get_times() == 0)
    {
        void *buf = buffer.get();
        io = new io_overlapped();
        io->wsaBuf.buf = (CHAR *)buf;
        io->wsaBuf.len = buffer.get_length();
        io->addr = target.get_raw_addr();
        io->addr_len = sizeof(io->addr);
        io->sock = (HANDLE)fd;
        param.set_user_ptr(io);
        DWORD len = 0;
        DWORD flag = 0;
        int err = WSARecvFrom(fd, (LPWSABUF)&io->wsaBuf, 1, &len, &flag, (SOCKADDR *)&io->addr, &io->addr_len,
                              &io->overlapped, 0);
        if (err == SOCKET_ERROR)
        {
            err = GetErr();
            if (err == ERROR_IO_PENDING)
                return {};
            else
                throw net_io_exception("aread from fail");
        }
    }
    target = socket_addr_t(io->addr);
    delete io;
    if (param.is_stop())
        return io_result::timeout;
    buffer.walk_step(buffer.get_length());
    buffer.finish_walk();
    return io_result::ok;
}

#endif

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
    if (type & event_type::readable || type & event_type::writable || type & event_type::error ||
        type & event_type::def)
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
#ifndef OS_WINDOWS
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        throw net_connect_exception("failed to start socket.", connection_state::no_resource);
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return new bsd_socket_t(fd);
#else
    int fd = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
    if (fd < 0)
        throw net_connect_exception("failed to start socket.", connection_state::no_resource);
    uint64_t flags = 1;
    if (ioctlsocket(fd, FIONBIO, (u_long *)&flags) == SOCKET_ERROR)
    {
        throw net_connect_exception("fail to set socket opt", connection_state::no_resource);
    }

    return new iocp_socket_t(fd);
#endif
}

socket_t *new_udp_socket()
{
#ifndef OS_WINDOWS
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        throw net_connect_exception("failed to start socket.", connection_state::no_resource);
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return new bsd_socket_t(fd);
#else
    int fd = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, 0, 0, WSA_FLAG_OVERLAPPED);
    if (fd < 0)
        throw net_connect_exception("failed to start socket.", connection_state::no_resource);
    uint64_t flags = 1;
    if (ioctlsocket(fd, FIONBIO, (u_long *)&flags) == SOCKET_ERROR)
    {
        throw net_connect_exception("fail to set socket opt", connection_state::no_resource);
    }
    // TODO: completion notification
    return new iocp_socket_t(fd);
#endif
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
#ifdef OS_WINDOWS

LPFN_CONNECTEX ConnectExFn = nullptr;
co::async_result_t<io_result> connect_to(co::paramter_t &param, socket_t *socket, socket_addr_t socket_to_addr)
{
    if (ConnectExFn == nullptr)
    {
        DWORD dwBytes;
        GUID guidConnectEx = WSAID_CONNECTEX;
        if (SOCKET_ERROR == WSAIoctl((SOCKET)socket->get_raw_handle(), SIO_GET_EXTENSION_FUNCTION_POINTER,
                                     &guidConnectEx, sizeof(guidConnectEx), &ConnectExFn, sizeof(ConnectExFn), &dwBytes,
                                     NULL, NULL))
        {
            throw net_io_exception("get connectEx fail");
        }
    }
    auto io = (io_overlapped *)param.get_user_ptr();
    socket_t *target = nullptr;
    target = socket;
    io_result ret = io_result::ok;
    if (param.get_times() == 0)
    {
        sockaddr_in local;
        local.sin_family = AF_INET;
        local.sin_addr.S_un.S_addr = INADDR_ANY;
        local.sin_port = 0;
        int err = bind((SOCKET)socket->get_raw_handle(), (sockaddr *)&local, sizeof(local));
        if (err)
        {
            throw net_io_exception("bind fail");
        }
        io = new io_overlapped();
        io->sock = (HANDLE)socket->get_raw_handle();
        // io->wsaBuf.buf = new char[64];
        // io->wsaBuf.len = 64;
        io->type = io_type::connect;
        io->data = target;
        param.set_user_ptr(io);
        socket->add_event(event_type::def);
        int len = sizeof(sockaddr_in) + 16;
        auto addr = socket_to_addr.get_raw_addr();
        DWORD dwWrite;
        err = ConnectExFn((SOCKET)socket->get_raw_handle(), (sockaddr *)&addr, sizeof(addr), 0, 0, &dwWrite,
                          (LPOVERLAPPED)&io->overlapped);
        if (err == SOCKET_ERROR || err == 0)
        {
            err = GetErr();
            if (err == ERROR_IO_PENDING)
            {
                return {};
            }
            else
            {
                ret = io_result::failed;
            }
        }
    }
    else
    {
        if (io->err == WSAETIMEDOUT)
        {
            ret = io_result::failed;
        }
        else if (io->done)
        {
            ret = io_result::ok;
        }
    }
    auto sock = io->sock;
    bool has_done = io->done;
    delete io;
    if (param.is_stop() && !has_done)
    {
        ret = io_result::timeout;
    }
    if (ret != io_result::ok)
    {
        return ret;
    }
    setsockopt(socket->get_raw_handle(), SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, (char *)&sock, sizeof(sock));
    target->is_connection_closed = false;
    return io_result::ok;
}

co::async_result_t<io_result> connect_udp(co::paramter_t &param, socket_t *socket, socket_addr_t socket_to_addr)
{
    return connect_to(param, socket, socket_to_addr);
}
#else
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
    if (e == EINPROGRESS || e == WOULDBLOCK)
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
#endif

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

#ifdef OS_WINDOWS
LPFN_ACCEPTEX AcceptExFn = nullptr;
co::async_result_t<socket_t *> accept_from(co::paramter_t &param, socket_t *socket)
{
    if (AcceptExFn == nullptr)
    {
        DWORD dwBytes;
        GUID guidAcceptEx = WSAID_ACCEPTEX;
        if (SOCKET_ERROR == WSAIoctl((SOCKET)socket->get_raw_handle(), SIO_GET_EXTENSION_FUNCTION_POINTER,
                                     &guidAcceptEx, sizeof(guidAcceptEx), &AcceptExFn, sizeof(AcceptExFn), &dwBytes,
                                     NULL, NULL))
        {
            throw net_io_exception("get acceptEx fail");
        }
    }
    auto io = (io_overlapped *)param.get_user_ptr();
    socket_t *target = nullptr;
    if (param.get_times() == 0)
    {
        target = new_tcp_socket();
        io = new io_overlapped();
        io->sock = (HANDLE)socket->get_raw_handle();
        io->wsaBuf.buf = new char[64];
        io->wsaBuf.len = 64;
        io->type = io_type::accept;
        io->data = target;
        param.set_user_ptr(io);
        socket->add_event(event_type::def);
        DWORD dwRecv;
        int len = sizeof(sockaddr_in) + 16;
        int err = AcceptExFn((SOCKET)socket->get_raw_handle(), (SOCKET)target->get_raw_handle(), io->wsaBuf.buf, 0, len,
                             len, &dwRecv, (LPOVERLAPPED)&io->overlapped);
        if (err == SOCKET_ERROR || err == 0)
        {
            err = GetErr();
            if (err == ERROR_IO_PENDING)
            {
                return {};
            }
            else
            {
                throw net_io_exception("accept returns fail");
            }
        }
    }
    else
    {
        target = (socket_t *)io->data;
    }
    target->is_connection_closed = false;
    auto sock = io->sock;
    bool has_done = io->done;
    delete[] io->wsaBuf.buf;
    delete io;

    if (param.is_stop() && !has_done)
    {
        close_socket(target);
        return 0;
    }
    setsockopt(socket->get_raw_handle(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)&sock, sizeof(sock));
    return target;
}
#else
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
        if (r == WOULDBLOCK)
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
    auto socket2 = new bsd_socket_t(fd);
    socket2->is_connection_closed = false;
    return socket2;
}
#endif

void close_socket(socket_t *socket)
{
    socket->remove_event(event_type::readable | event_type::writable | event_type::error);
    delete socket;
}

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
