#include "net/socket_buffer.hpp"
#include <string.h>

namespace net
{
socket_buffer_t::socket_buffer_t(std::string str)
    : ptr(new byte[str.size()])
    , buffer_len(str.size())
    , data_len(str.size())
{
    memccpy(ptr, str.c_str(), str.size(), str.size());
}

socket_buffer_t::socket_buffer_t(long len)
    : ptr(new byte[len])
    , buffer_len(len)
    , data_len(0)
{
}

socket_buffer_t::~socket_buffer_t()
{
    if (ptr != nullptr)
        delete[] ptr;
}
} // namespace net
