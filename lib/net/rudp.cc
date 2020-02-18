#include "net/rudp.hpp"
#include "net/co.hpp"
#include "net/event.hpp"
#include "net/socket.hpp"
#include "net/third/ikcp.hpp"
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace net
{

int udp_output(const char *buf, int len, ikcpcb *kcp, void *user);

struct rudp_endpoint_t
{
    socket_addr_t remote_address;
    ikcpcb *ikcp;
    rudp_impl_t *impl;
    microsecond_t last_alive;
    microsecond_t inactive_timeout;
    /// next timer
    timer_registered_t timer_reg;
};

struct hash_so_t
{
    u64 operator()(const socket_addr_t &r) const { return r.hash(); }
};

class rudp_impl_t
{
    std::unordered_map<socket_addr_t, std::unique_ptr<rudp_endpoint_t>, hash_so_t> user_map;

    event_context_t *context;
    rudp_t::data_handler_t handler;
    rudp_t::unknown_handler_t unknown_handler;
    rudp_t::timeout_handler_t timeout_handler;

    friend int udp_output(const char *buf, int len, ikcpcb *kcp, void *user);
    socket_t *socket;

    /// set base time to aviod int overflow which used by kcp
    microsecond_t base_time;
    co::paramter_t co_param;

    void set_timer(rudp_endpoint_t *ep)
    {
        auto cur = get_current_time();
        auto next_tick_time = ikcp_check(ep->ikcp, (cur - base_time) / 1000);
        auto time_point = next_tick_time * 1000 + base_time;
        if (time_point < cur)
            time_point = cur;
        if (ep->timer_reg.id >= 0 && ep->timer_reg.timepoint <= time_point + 5000 &&
            ep->timer_reg.timepoint >= time_point - 5000)
        {
            // no need change timer
            return;
        }

        if (ep->timer_reg.id >= 0)
        {
            socket->get_event_loop().remove_timer(ep->timer_reg);
        }

        ep->timer_reg = socket->get_event_loop().add_timer(make_timer(time_point - cur, [this, ep]() {
            ikcp_update(ep->ikcp, (get_current_time() - base_time) / 1000);
            if (!co_param.is_stop())
            {
                socket->get_coroutine()->resume();
            }
            set_timer(ep);
        }));
    }

  public:
    rudp_impl_t()
    {
        socket = new_udp_socket();
        base_time = get_current_time();
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

    void bind(event_context_t &context)
    {
        this->context = &context;
        socket_addr_t address(0);
        bind_at(socket, address);
        context.add_socket(socket);
    }

    void on_unknown_connection(rudp_t::unknown_handler_t handler) { this->unknown_handler = handler; }

    void on_timeout_connection(rudp_t::timeout_handler_t handler) { this->timeout_handler = handler; }

    void run(std::function<void()> func) { socket->startup_coroutine(co::coroutine_t::create(func)); }

    void add_connection(socket_addr_t addr, microsecond_t inactive_timeout)
    {
        if (user_map.find(addr) != user_map.end())
            return;
        std::unique_ptr<rudp_endpoint_t> endpoint = std::make_unique<rudp_endpoint_t>();
        auto pcb = ikcp_create(1, endpoint.get());
        endpoint->ikcp = pcb;
        endpoint->inactive_timeout = inactive_timeout;
        endpoint->remote_address = addr;
        endpoint->impl = this;
        endpoint->timer_reg.id = -1;
        ikcp_setoutput(pcb, udp_output);
        ikcp_wndsize(pcb, 128, 128);
        // fast mode
        ikcp_nodelay(pcb, 1, 10, 2, 1);
        pcb->rx_minrto = 10;
        pcb->fastresend = 1;
        user_map.insert(std::make_pair(addr, std::move(endpoint)));
    }

    bool removeable(socket_addr_t addr)
    {
        auto it = user_map.find(addr);
        if (it == user_map.end())
            return false;
        return ikcp_waitsnd(it->second->ikcp) == 0;
    }

    void remove_connection(socket_addr_t addr)
    {
        auto it = user_map.find(addr);
        if (it == user_map.end())
            return;

        ikcp_update(it->second->ikcp, (get_current_time() - base_time) / 1000);
        ikcp_release(it->second->ikcp);
    }

    co::async_result_t<io_result> awrite(co::paramter_t &param, socket_buffer_t &buffer, socket_addr_t address)
    {
        assert(buffer.get_data_length() <= INT32_MAX);
        auto endpoint_it = user_map.find(address);
        if (endpoint_it == user_map.end())
            return io_result::failed;
        auto pcb = endpoint_it->second->ikcp;
        ikcp_send(pcb, (const char *)buffer.get_raw_ptr(), buffer.get_data_length());
        set_timer(endpoint_it->second.get());
        endpoint_it->second->last_alive = get_current_time();
        return io_result::ok;
    }

    co::async_result_t<io_result> aread(co::paramter_t &param, socket_buffer_t &buffer, socket_addr_t &address)
    {
        if (param.is_stop()) /// stop timeout
        {
            socket_addr_t target;
            co_param.stop_wait();
            socket_buffer_t recv_buffer(nullptr, 0);
            /// just stop timeout, not really read to buffer
            socket_aread_from(co_param, socket, recv_buffer, target);
            buffer.end_process();
            return io_result::timeout;
        }
        if (co_param.is_stop())
        {
            co_param = {}; /// new
        }

        socket_buffer_t recv_buffer(1472);

        recv_buffer.expect().origin_length();
        socket_addr_t target;

        std::unordered_set<rudp_endpoint_t *> areads;

        // read data no wait from kernel udp buffer
        while (socket_aread_from(co_param, socket, recv_buffer, target).is_finish())
        {
            auto endpoint_it = user_map.find(target);
            if (endpoint_it == user_map.end())
            {
                if (unknown_handler)
                {
                    if (!unknown_handler(target))
                    {
                        // discard packet
                        recv_buffer.expect().length(0);
                        break;
                    }
                }
            }
            endpoint_it->second->last_alive = get_current_time();
            areads.insert(endpoint_it->second.get());
            // udp -> ikcp
            ikcp_input(endpoint_it->second->ikcp, (char *)recv_buffer.get_raw_ptr(), recv_buffer.get_data_length());

            // reset param
            co_param = {};
            recv_buffer.expect().origin_length();
        }
        /// reset timer
        for (auto it : areads)
        {
            set_timer(it);
        }

        for (auto it : areads)
        {
            // data <- KCP <- UDP

            auto len = ikcp_recv(it->ikcp, (char *)buffer.get_raw_ptr(), buffer.get_data_length());
            if (len >= 0)
            {
                buffer.set_process_length(len);
                buffer.end_process();
                co_param.stop_wait();
                // stop recv aread event.
                socket_aread_from(co_param, socket, recv_buffer, target);
                co_param = {};
                address = it->remote_address;
                return io_result::ok;
            }
        }

        return {};
    }

    socket_t *get_socket() const { return socket; }

    void close_all_peer()
    {
        auto ts = (get_current_time() - base_time) / 1000;
        for (auto &it : user_map)
        {
            ikcp_update(it.second->ikcp, ts);
            ikcp_release(it.second->ikcp);
        }
    }

    void close()
    {
        if (!socket)
            return;
        close_all_peer();
        close_socket(socket);

        socket = nullptr;
    }

    ~rudp_impl_t() { close(); }
};

class rudp_acceptor_impl_t
{
    event_context_t *context;
    rudp_t::data_handler_t handler;
    ikcpcb *pcb;
    friend int udp_output(const char *buf, int len, ikcpcb *kcp, void *user);
    socket_t *socket;

  public:
    void bind(event_context_t &context, socket_addr_t bind_addr, bool reuse_addr = false) {}
};

int udp_output(const char *buf, int len, ikcpcb *kcp, void *user)
{
    rudp_endpoint_t *endpoint = (rudp_endpoint_t *)user;
    socket_buffer_t buffer((byte *)(buf), len);
    buffer.expect().origin_length();
    endpoint->last_alive = get_current_time();
    // output data to kernel, sendto udp will return immediately forever.
    // so there is no need to switch to socket coroutine.
    if (co::await(socket_awrite_to, endpoint->impl->socket, buffer, endpoint->remote_address) == io_result::ok)
        return 0;
    // send failed when kernel buffer is full.
    // KCP will not receive this package's ACK.
    // trigger resend after next tick
    return -1;
}

/// rudp ---------------------------

rudp_t::rudp_t() { impl = new rudp_impl_t(); }

rudp_t::~rudp_t() { delete impl; }

void rudp_t::bind(event_context_t &context, socket_addr_t addr, bool reuse_addr)
{
    impl->bind(context, addr, reuse_addr);
}

/// bind random port
void rudp_t::bind(event_context_t &context) { impl->bind(context); }

void rudp_t::add_connection(socket_addr_t addr, microsecond_t inactive_timeout)
{
    impl->add_connection(addr, inactive_timeout);
}

bool rudp_t::removeable(socket_addr_t addr) { return impl->removeable(addr); }

void rudp_t::remove_connection(socket_addr_t addr) { impl->remove_connection(addr); }

rudp_t &rudp_t::on_unknown_packet(unknown_handler_t handler)
{
    impl->on_unknown_connection(handler);
    return *this;
}

rudp_t &rudp_t::on_connection_timeout(timeout_handler_t handler)
{
    impl->on_timeout_connection(handler);
    return *this;
}

void rudp_t::run(std::function<void()> func) { impl->run(func); }

socket_t *rudp_t::get_socket() const { return impl->get_socket(); }

void rudp_t::close_all_remote() { impl->close_all_peer(); }

void rudp_t::close() { impl->close(); }

/// wrappers -----------------------

co::async_result_t<io_result> rudp_t::awrite(co::paramter_t &param, socket_buffer_t &buffer, socket_addr_t address)
{
    return impl->awrite(param, buffer, address);
}

co::async_result_t<io_result> rudp_t::aread(co::paramter_t &param, socket_buffer_t &buffer, socket_addr_t &address)
{
    return impl->aread(param, buffer, address);
}

co::async_result_t<io_result> rudp_awrite(co::paramter_t &param, rudp_t *rudp, socket_buffer_t &buffer,
                                          socket_addr_t address)
{
    return rudp->awrite(param, buffer, address);
}

co::async_result_t<io_result> rudp_aread(co::paramter_t &param, rudp_t *rudp, socket_buffer_t &buffer,
                                         socket_addr_t &address)
{
    return rudp->aread(param, buffer, address);
}

} // namespace net
