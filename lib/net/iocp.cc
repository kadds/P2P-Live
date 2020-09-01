#include "net/iocp.hpp"
#ifdef OS_WINDOWS

namespace net
{

io_overlapped::io_overlapped()
{
    memset(&overlapped, 0, sizeof(overlapped));
    sock = 0;
    addr_len = 0;
    err = 0;
    buffer_do_len = 0;
    data = 0;
    wsaBuf.len = 0;
    wsaBuf.buf = 0;
    done = false;
}

handle_t event_iocp_demultiplexer::make() { return (int)CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0); }

void event_iocp_demultiplexer::close(handle_t handle) { CloseHandle((HANDLE)handle); }

event_iocp_demultiplexer::event_iocp_demultiplexer(handle_t h) { iocp_handle = h; }

event_iocp_demultiplexer::~event_iocp_demultiplexer() {}

void event_iocp_demultiplexer::add(handle_t handle, event_type_t type)
{
    if (map.count(handle) == 0)
    {
        CreateIoCompletionPort((HANDLE)handle, (HANDLE)iocp_handle, handle, 0);
    }
}

handle_t event_iocp_demultiplexer::select(event_type_t *type, microsecond_t *timeout)
{
    DWORD ioSize = 0;
    void *key = NULL;
    LPOVERLAPPED lpOverlapped = NULL;
    int err = 0;
    if (FALSE ==
        GetQueuedCompletionStatus((HANDLE)iocp_handle, &ioSize, (PULONG_PTR)&key, &lpOverlapped, *timeout / 1000))
    {
        err = WSAGetLastError();
    }
    if ((ioSize == 1) && lpOverlapped == NULL)
    {
        // wakeup
        *timeout = 0;
        return 0;
    }
    io_overlapped *io = (io_overlapped *)lpOverlapped;
    // timeout
    if (io == nullptr)
    {
        *timeout = 0;
        if (key == nullptr)
        {
            return 0;
        }
        return *(int *)key;
    }
    *type = event_type::def;
    io->buffer_do_len = ioSize;
    io->err = err;
    io->done = true;
    return (handle_t)io->sock;
}

void event_iocp_demultiplexer::remove(handle_t handle, event_type_t type)
{
    CancelIo((HANDLE)handle);
    if (map.count(handle) == 0)
    {
        return;
    }
    map.erase(handle);
}

void event_iocp_demultiplexer::wake_up(event_loop_t &cur_loop)
{
    PostQueuedCompletionStatus((HANDLE)iocp_handle, 1, 0, 0);
}

} // namespace net
#endif