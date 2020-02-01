#pragma once
#include "net/net.hpp"
namespace net
{
struct socket_buffer_t;

struct except_buffer_helper_t
{
    socket_buffer_t *buf;
    explicit except_buffer_helper_t(socket_buffer_t *buf)
        : buf(buf)
    {
    }

    except_buffer_helper_t length(long len);
    except_buffer_helper_t origin_length();

    socket_buffer_t &operator()() { return *buf; }
};
class socket_buffer_t
{
    byte *ptr;
    long buffer_len;
    long data_len;
    long current_process;
    friend struct except_buffer_helper_t;

  public:
    socket_buffer_t(std::string str);
    socket_buffer_t(long len);
    socket_buffer_t(const socket_buffer_t &) = delete;
    socket_buffer_t &operator=(const socket_buffer_t &) = delete;

    ~socket_buffer_t();

    byte *get() const { return ptr; }
    long get_data_length() const { return data_len; }
    long get_buffer_length() const { return buffer_len; }
    long get_process_length() const { return current_process; }

    void set_process_length(long len) { current_process = len; }

    except_buffer_helper_t expect() { return except_buffer_helper_t(this); }
};

}; // namespace net