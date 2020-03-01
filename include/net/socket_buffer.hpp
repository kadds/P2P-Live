#pragma once
#include "net/net.hpp"
#include <atomic>
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

/// a control block used by socket buffer
struct socket_buffer_header_t
{
    std::atomic_int ref_count;
};

class socket_buffer_t
{
    byte *ptr;
    socket_buffer_header_t *header;
    u64 buffer_size;
    u64 valid_data_length;
    u64 walk_offset;

    friend struct except_buffer_helper_t;

  public:
    socket_buffer_t(std::string str);
    /// init buffer with nothing, the pointer will not be initialized.
    socket_buffer_t();
    socket_buffer_t(u64 len);
    /// init buffer with pointer
    socket_buffer_t(byte *buffer_ptr, u64 buffer_length);

    /// copy
    /// add ref
    socket_buffer_t(const socket_buffer_t &);
    socket_buffer_t &operator=(const socket_buffer_t &);

    // move operation
    socket_buffer_t(socket_buffer_t &&buf);
    socket_buffer_t &operator()(socket_buffer_t &&buf);

    ~socket_buffer_t();

    byte *get_base_ptr() const { return ptr; }
    /// get pointer current offset
    byte *get() const { return ptr + walk_offset; }

    u64 get_data_length() const { return valid_data_length; }
    u64 get_buffer_origin_length() const { return buffer_size; }

    /// get data length start at current offset
    u64 get_length() const { return valid_data_length - walk_offset; }

    u64 get_walk_offset() const { return walk_offset; }

    /// reset offset and set data length to offset
    void finish_walk()
    {
        valid_data_length = walk_offset;
        walk_offset = 0;
    }

    // except size to read/write
    except_buffer_helper_t expect() { return except_buffer_helper_t(this); }

    long write_string(const std::string &str);
    std::string to_string() const;

    /// walk in buffer
    void walk_step(u64 delta)
    {
        walk_offset += delta;
        if (walk_offset > valid_data_length)
            walk_offset = valid_data_length;
    }

    void clear();
};

}; // namespace net