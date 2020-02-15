#pragma once
#include "co.hpp"
#include "net.hpp"
#include "socket_addr.hpp"
#include "socket_buffer.hpp"
#include <functional>
#include <memory>

namespace net
{
class event_context_t;
class rudp_impl_t;
class rudp_t;

using data_handler_t = std::function<void(rudp_t &, socket_buffer_t buffer)>;

class rudp_t
{
    // impl idiom
    rudp_impl_t *kcp_impl;

  public:
    rudp_t();
    ~rudp_t();

    /// Do not copy
    rudp_t(const rudp_t &) = delete;
    rudp_t &operator=(const rudp_t &) = delete;

    void bind(event_context_t &context, socket_addr_t local_addr, bool reuse_addr = false);

    /// set remote address to receive
    void connect(socket_addr_t addr);

    /// call bind before call this function
    void run(std::function<void()> func);

    co::async_result_t<io_result> awrite(co::paramter_t &param, socket_buffer_t &buffer);
    co::async_result_t<io_result> aread(co::paramter_t &param, socket_buffer_t &buffer);

    socket_t *get_socket();
};

// wrapper functions
co::async_result_t<io_result> rudp_awrite(co::paramter_t &param, rudp_t *rudp, socket_buffer_t &buffer);
co::async_result_t<io_result> rudp_aread(co::paramter_t &param, rudp_t *rudp, socket_buffer_t &buffer);

} // namespace net
