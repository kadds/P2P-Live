#include "net/socket_buffer.hpp"
#include <string.h>

namespace net
{
except_buffer_helper_t except_buffer_helper_t::length(long len)
{
    buf->data_len = len;
    buf->current_process = 0;
    return *this;
}

except_buffer_helper_t except_buffer_helper_t::origin_length()
{
    buf->data_len = buf->buffer_len;
    buf->current_process = 0;
    return *this;
}

socket_buffer_t::socket_buffer_t(std::string str)
    : ptr(new byte[str.size()])
    , buffer_len(str.size())
    , data_len(0)
    , current_process(0)
{
    memccpy(ptr, str.c_str(), str.size(), str.size());
}

socket_buffer_t::socket_buffer_t(long len)
    : ptr(new byte[len])
    , buffer_len(len)
    , data_len(0)
    , current_process(0)
{
}

socket_buffer_t::~socket_buffer_t()
{
    if (ptr != nullptr)
        delete[] ptr;
}
} // namespace net
