#include "net/socket_buffer.hpp"
#include <string.h>

namespace net
{
socket_buffer_t::except_buffer_helper_t socket_buffer_t::except_buffer_helper_t::length(u64 len)
{
    buf->valid_data_length = len;
    buf->walk_offset = 0;
    return *this;
}

socket_buffer_t::except_buffer_helper_t socket_buffer_t::except_buffer_helper_t::origin_length()
{
    buf->valid_data_length = buf->buffer_size;
    buf->walk_offset = 0;
    return *this;
}

socket_buffer_t::socket_buffer_t()
    : ptr(nullptr)
    , header(nullptr)
    , buffer_size(0)
    , valid_data_length(0)
    , walk_offset(0)
{
}

socket_buffer_t::socket_buffer_t(u64 len)
    : ptr(new byte[len])
    , header(new socket_buffer_header_t())
    , buffer_size(len)
    , valid_data_length(0)
    , walk_offset(0)
{
    header->ref_count = 1;
}

socket_buffer_t socket_buffer_t::from_struct_inner(byte *buffer_ptr, u64 buffer_length)
{
    socket_buffer_t buffer(buffer_ptr, buffer_length);
    return std::move(buffer);
}

socket_buffer_t socket_buffer_t::from_string(std::string str)
{
    socket_buffer_t buffer(str.size());
    memcpy(buffer.ptr, str.c_str(), str.size());
    return std::move(buffer);
}

socket_buffer_t::socket_buffer_t(byte *buffer_ptr, u64 buffer_length)
    : ptr(buffer_ptr)
    , header(nullptr)
    , buffer_size(buffer_length)
    , valid_data_length(0)
    , walk_offset(0)
{
}

socket_buffer_t::socket_buffer_t(const socket_buffer_t &rh)
{
    this->ptr = rh.ptr;
    this->valid_data_length = rh.valid_data_length;
    this->buffer_size = rh.buffer_size;
    this->walk_offset = rh.walk_offset;
    this->header = rh.header;
    if (this->header)
        this->header->ref_count++;
}

socket_buffer_t &socket_buffer_t::operator=(const socket_buffer_t &rh)
{
    if (&rh == this)
        return *this;

    this->ptr = rh.ptr;
    this->valid_data_length = rh.valid_data_length;
    this->buffer_size = rh.buffer_size;
    this->walk_offset = rh.walk_offset;
    this->header = rh.header;
    if (this->header)
        this->header->ref_count++;
    return *this;
}

socket_buffer_t::socket_buffer_t(socket_buffer_t &&buffer)
{
    this->ptr = buffer.ptr;
    this->valid_data_length = buffer.valid_data_length;
    this->buffer_size = buffer.buffer_size;
    this->walk_offset = buffer.walk_offset;
    this->header = buffer.header;

    buffer.ptr = nullptr;
    buffer.header = nullptr;
    buffer.valid_data_length = 0;
    buffer.buffer_size = 0;
    buffer.walk_offset = 0;
}

socket_buffer_t &socket_buffer_t::operator()(socket_buffer_t &&buffer)
{
    if (this->ptr)
        delete[] ptr;

    this->ptr = buffer.ptr;
    this->valid_data_length = buffer.valid_data_length;
    this->buffer_size = buffer.buffer_size;
    this->walk_offset = buffer.walk_offset;
    this->header = buffer.header;

    buffer.ptr = nullptr;
    buffer.header = nullptr;
    buffer.valid_data_length = 0;
    buffer.buffer_size = 0;
    buffer.walk_offset = 0;

    return *this;
}

socket_buffer_t::~socket_buffer_t()
{
    if (ptr && header && --header->ref_count == 0)
    {
        delete[] ptr;
        delete header;
    }
}

long socket_buffer_t::write_string(const std::string &str)
{
    auto len = str.size();
    if (len > get_length())
        len = get_length();

    memcpy(get(), str.c_str(), len);

    return len;
}

std::string socket_buffer_t::to_string() const
{
    std::string str;
    str.resize(get_length());
    byte *start_ptr = get();
    for (long i = 0; i < str.size(); i++)
    {
        str[i] = *((char *)start_ptr + i);
    }
    return str;
}

void socket_buffer_t::clear()
{
    if (ptr)
    {
        auto start_ptr = get();
        auto size = get_length();
        memset(start_ptr, 0, size);
    }
}

} // namespace net
