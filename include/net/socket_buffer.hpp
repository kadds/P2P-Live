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

    except_buffer_helper_t length(u64 len);
    except_buffer_helper_t origin_length();

    socket_buffer_t &operator()() const { return *buf; }
};

class socket_buffer_t
{
    byte *ptr;
    u64 buffer_len;
    u64 data_len;
    u64 current_process;
    u64 walk_offset;
    friend struct except_buffer_helper_t;

  public:
    socket_buffer_t(std::string str);
    socket_buffer_t(u64 len);
    socket_buffer_t(socket_buffer_t &&buffer);
    socket_buffer_t(const socket_buffer_t &) = delete;
    socket_buffer_t &operator=(const socket_buffer_t &) = delete;
    socket_buffer_t &operator=(socket_buffer_t &&buffer);
    ~socket_buffer_t();

    byte *get_raw_ptr() const { return ptr; }
    byte *get_step_ptr() { return ptr + walk_offset; }
    u64 get_data_length() const { return data_len; }
    u64 get_step_rest_length() const { return data_len - walk_offset; }
    u64 get_buffer_length() const { return buffer_len; }
    u64 get_process_length() const { return current_process; }

    void set_process_length(u64 len) { current_process = len; }
    void end_process() { data_len = current_process; }

    except_buffer_helper_t expect()
    {
        walk_offset = 0;
        return except_buffer_helper_t(this);
    }

    long write_string(const std::string &str);
    std::string to_string() const;

    void walk_step(u64 delta)
    {
        walk_offset += delta;
        if (walk_offset > data_len)
        {
            walk_offset = data_len;
        }
    }
};

}; // namespace net