#pragma once
#include "net/net.hpp"
namespace net
{
class socket_buffer_t
{
    byte *ptr;
    long data_len;
    long buffer_len;

  public:
    socket_buffer_t(std::string str);
    socket_buffer_t(long len);
    socket_buffer_t(const socket_buffer_t &) = delete;
    socket_buffer_t &operator=(const socket_buffer_t &) = delete;

    ~socket_buffer_t();

    byte *get() { return ptr; }
    long get_data_len() { return data_len; }
    long get_buffer_len() { return buffer_len; }
    void set_data_len(long len) { data_len = len; }
};
}; // namespace net