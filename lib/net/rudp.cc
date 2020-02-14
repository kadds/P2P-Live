#include "net/rudp.hpp"
#include "net/co.hpp"
#include "net/event.hpp"
#include "net/socket.hpp"
#include "net/third/ikcp.hpp"

namespace net
{

int udp_output(const char *buf, int len, ikcpcb *kcp, void *user);

class rudp_impl_t
{
    event_context_t *context;
    data_handler_t handler;
    ikcpcb *pcb;
    friend int udp_output(const char *buf, int len, ikcpcb *kcp, void *user);
    socket_t *socket;
    socket_addr_t remote_addr;
    /// next timer
    timer_id timerid;
    microsecond_t next_time_point;
    /// set base time to aviod int overflow which used by kcp
    microsecond_t base_time;
    bool is_await;
    void set_timer()
    {
        auto next_tick_time = ikcp_check(pcb, (get_current_time() - base_time) / 1000);
        if (timerid >= 0 && next_tick_time * 1000 + base_time == next_time_point)
        {
            // no need change timer
            return;
        }

        if (timerid >= 0)
        {
            socket->get_event_loop().remove_timer(next_time_point, timerid);
        }

        next_time_point = (u64)next_tick_time * 1000 + base_time;

        timerid = socket->get_event_loop().add_timer(make_timer(next_time_point - get_current_time(), [this]() {
            ikcp_update(pcb, (get_current_time() - base_time) / 1000);
            if (is_await)
            {
                socket->get_coroutine()->resume();
            }
            set_timer();
        }));
    }

  public:
    rudp_impl_t()
        : pcb(nullptr)
    {
        socket = new_udp_socket();
        pcb = ikcp_create(1, this);
        ikcp_setoutput(pcb, udp_output);
        ikcp_wndsize(pcb, 128, 128);
        // fast mode
        ikcp_nodelay(pcb, 1, 50, 1, 0);
        base_time = get_current_time();
        timerid = -1;
        is_await = false;
    }

    rudp_impl_t(const rudp_impl_t &) = delete;
    rudp_impl_t &operator=(const rudp_impl_t &) = delete;

    void bind(event_context_t &context, socket_addr_t addr, bool reuse_addr)
    {
        this->context = &context;
        bind_at(socket, addr);
        if (reuse_addr)
            reuse_addr_socket(socket, true);
        context.add_socket(socket);
    }

    void run(std::function<void()> func) { socket->startup_coroutine(co::coroutine_t::create(func)); }

    void connect_to_remote(socket_addr_t addr) { remote_addr = addr; }

    co::async_result_t<io_result> awrite(co::paramter_t &param, socket_buffer_t &buffer)
    {
        assert(buffer.get_data_length() <= INT32_MAX);
        ikcp_send(pcb, (const char *)buffer.get_raw_ptr(), buffer.get_data_length());
        set_timer();
        return io_result::ok;
    }

    co::async_result_t<io_result> aread(co::paramter_t &param, socket_buffer_t &buffer)
    {
        if (param.is_stop()) /// stop timeout
        {
            is_await = false;
            return io_result::timeout;
        }

        socket_buffer_t recv_buffer(1472);

        recv_buffer.expect().origin_length();
        socket_addr_t target;

        // read data no wait from kernel udp buffer
        co::paramter_t param2;
        while (socket_aread_from(param2, socket, recv_buffer, target).is_finish())
        {
            if (target == remote_addr)
                ikcp_input(pcb, (char *)recv_buffer.get_raw_ptr(), recv_buffer.get_data_length());
            else
            {
                /// TODO: invalid user connect to it. report an error or run a callback
            }
            // reset param
            param2 = co::paramter_t();
            recv_buffer.expect().origin_length();
        }
        param2.stop_wait();
        // stop recv aread event.
        socket_aread_from(param, socket, recv_buffer, target);

        // normal receive data from KCP
        auto len = ikcp_recv(pcb, (char *)buffer.get_raw_ptr(), buffer.get_data_length());
        if (len < 0) // EAGAIN
        {
            is_await = true;
            // add event
            return co::async_result_t<io_result>();
        }
        buffer.set_process_length(len);
        buffer.end_process();
        is_await = false;

        set_timer();

        return io_result::ok;
    }

    ~rudp_impl_t()
    {
        if (pcb)
            ikcp_release(pcb);
    }
};

class rudp_acceptor_impl_t
{
    event_context_t *context;
    data_handler_t handler;
    ikcpcb *pcb;
    friend int udp_output(const char *buf, int len, ikcpcb *kcp, void *user);
    socket_t *socket;

  public:
    void bind(event_context_t &context, socket_addr_t bind_addr, bool reuse_addr = false) {}
};

int udp_output(const char *buf, int len, ikcpcb *kcp, void *user)
{
    rudp_impl_t *impl = (rudp_impl_t *)user;
    socket_buffer_t buffer((byte *)(buf), len);
    buffer.expect().origin_length();
    // output data to kernel, every send udp will return immediately.
    // so there is no need to switch to socket coroutine.
    if (co::await(socket_awrite_to, impl->socket, buffer, impl->remote_addr) == io_result::ok)
        return 0;
    // send failed when udp buffer is full. so stop send it and
    // KCP will not receive this package ACK and resend this package later
    return -1;
}

/// rudp ---------------------------

rudp_t::rudp_t() { kcp_impl = new rudp_impl_t(); }

rudp_t::~rudp_t() { delete kcp_impl; }

void rudp_t::bind(event_context_t &context, socket_addr_t addr, bool reuse_addr)
{
    kcp_impl->bind(context, addr, reuse_addr);
}

void rudp_t::connect(socket_addr_t addr) { kcp_impl->connect_to_remote(addr); }

void rudp_t::run(std::function<void()> func) { kcp_impl->run(func); }

/// wrappers -----------------------

co::async_result_t<io_result> rudp_t::awrite(co::paramter_t &param, socket_buffer_t &buffer)
{
    return kcp_impl->awrite(param, buffer);
}

co::async_result_t<io_result> rudp_t::aread(co::paramter_t &param, socket_buffer_t &buffer)
{
    return kcp_impl->aread(param, buffer);
}

co::async_result_t<io_result> rudp_awrite(co::paramter_t &param, rudp_t *rudp, socket_buffer_t &buffer)
{
    return rudp->awrite(param, buffer);
}

co::async_result_t<io_result> rudp_aread(co::paramter_t &param, rudp_t *rudp, socket_buffer_t &buffer)
{
    return rudp->awrite(param, buffer);
}

} // namespace net
