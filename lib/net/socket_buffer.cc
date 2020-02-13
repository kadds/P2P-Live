#include "net/socket_buffer.hpp"
#include <string.h>

namespace net
{
except_buffer_helper_t except_buffer_helper_t::length(u64 len)
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
    , walk_offset(0)
    , own_ptr(true)
{
    memcpy(ptr, str.c_str(), str.size());
}

socket_buffer_t::socket_buffer_t(u64 len)
    : ptr(new byte[len])
    , buffer_len(len)
    , data_len(0)
    , current_process(0)
    , walk_offset(0)
    , own_ptr(true)
{
}

socket_buffer_t::socket_buffer_t(byte *buffer_ptr, u64 buffer_length)
    : ptr(buffer_ptr)
    , buffer_len(buffer_length)
    , data_len(0)
    , current_process(0)
    , walk_offset(0)
    , own_ptr(false)
{
}

socket_buffer_t::socket_buffer_t(socket_buffer_t &&buffer)
{
    this->ptr = buffer.ptr;
    this->current_process = buffer.current_process;
    this->data_len = buffer.data_len;
    this->buffer_len = buffer.buffer_len;
    this->walk_offset = buffer.walk_offset;
    this->own_ptr = buffer.own_ptr;

    buffer.ptr = nullptr;
    buffer.current_process = 0;
    buffer.data_len = 0;
    buffer.buffer_len = 0;
    buffer.walk_offset = 0;
    buffer.own_ptr = false;
}

socket_buffer_t &socket_buffer_t::operator()(socket_buffer_t &&buf)
{
    if (this->ptr)
        delete[] ptr;

    this->ptr = buf.ptr;
    this->current_process = buf.current_process;
    this->data_len = buf.data_len;
    this->buffer_len = buf.buffer_len;
    this->walk_offset = buf.walk_offset;
    this->own_ptr = buf.own_ptr;

    buf.ptr = nullptr;
    buf.current_process = 0;
    buf.data_len = 0;
    buf.buffer_len = 0;
    buf.walk_offset = 0;
    buf.own_ptr = false;

    return *this;
}

socket_buffer_t::~socket_buffer_t()
{
    if (ptr && own_ptr)
        delete[] ptr;
}

long socket_buffer_t::write_string(const std::string &str)
{
    long len = str.size();
    if (len > buffer_len)
        len = buffer_len;

    memcpy(ptr, str.c_str(), len);
    return len;
}

std::string socket_buffer_t::to_string() const
{
    std::string str;
    str.resize(data_len);
    for (long i = 0; i < data_len; i++)
    {
        str[i] = *((char *)ptr + i);
    }
    return str;
}

} // namespace net
