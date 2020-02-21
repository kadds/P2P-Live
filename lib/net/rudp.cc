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
    int channel;
    rudp_impl_t *impl;
    microsecond_t last_alive;
    microsecond_t inactive_timeout;
    /// next timer
    timer_registered_t timer_reg;
    bool wait_for_io;
    execute_context_t econtext;
};

struct hash_so_t
{
    u64 operator()(const socket_addr_t &r) const { return r.hash(); }
};

class rudp_impl_t
{
    std::unordered_map<socket_addr_t, std::unordered_map<int, std::unique_ptr<rudp_endpoint_t>>, hash_so_t> user_map;

    event_context_t *context;
    rudp_t::unknown_handler_t unknown_handler;
    rudp_t::timeout_handler_t timeout_handler;
    rudp_t::new_connection_handler_t new_connection_handler;

    friend int udp_output(const char *buf, int len, ikcpcb *kcp, void *user);
    socket_t *socket;
    /// set base time to aviod int overflow which used by kcp
    microsecond_t base_time;
    socket_buffer_t recv_buffer;

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
            ep->econtext.get_loop()->remove_timer(ep->timer_reg);
        }

        ep->timer_reg = ep->econtext.get_loop()->add_timer(make_timer(time_point - cur, [this, ep]() {
            ikcp_update(ep->ikcp, (get_current_time() - base_time) / 1000);
            if (ep->wait_for_io)
                ep->econtext.start();
            set_timer(ep);
        }));
    }

  public:
    rudp_impl_t()
        : recv_buffer(1472)
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
        socket->bind_context(context);
        socket->run(std::bind(&rudp_impl_t::rudp_server_main, this));
    }

    void bind(event_context_t &context)
    {
        this->context = &context;
        socket_addr_t address(0);
        bind_at(socket, address);
        socket->bind_context(context);
        socket->run(std::bind(&rudp_impl_t::rudp_server_main, this));
    }

    void config(rudp_connection_t conn, int level)
    {
        auto endpoint = find(conn);
        if (!endpoint)
            return;
        if (level == 0)
        {
            // fast mode
            ikcp_nodelay(endpoint->ikcp, 1, 10, 2, 1);
        }
        else if (level == 1)
        {
            ikcp_nodelay(endpoint->ikcp, 1, 20, 3, 1);
        }
        else
        {
            ikcp_nodelay(endpoint->ikcp, 0, 50, 0, 0);
        }
    }

    void on_unknown_connection(rudp_t::unknown_handler_t handler) { this->unknown_handler = handler; }

    void on_timeout_connection(rudp_t::timeout_handler_t handler) { this->timeout_handler = handler; }

    void on_new_connection(rudp_t::new_connection_handler_t handler) { new_connection_handler = handler; }

    rudp_endpoint_t *find(rudp_connection_t conn)
    {
        auto it = user_map.find(conn.address);
        if (it != user_map.end())
        {
            auto it2 = it->second.find(conn.channel);
            if (it2 != it->second.end())
            {
                return it2->second.get();
            }
        }
        return nullptr;
    }

    rudp_endpoint_t *find(socket_addr_t address, int channel)
    {
        auto it = user_map.find(address);
        if (it != user_map.end())
        {
            auto it2 = it->second.find(channel);
            if (it2 != it->second.end())
            {
                return it2->second.get();
            }
        }
        return nullptr;
    }

    void run_at(rudp_connection_t conn, std::function<void()> func)
    {
        auto endpoint = find(conn);
        if (endpoint == nullptr)
            return;
        endpoint->econtext.start_with(func);
    }

    void add_connection(socket_addr_t addr, int channel, microsecond_t inactive_timeout,
                        std::function<void(rudp_connection_t)> co_func)
    {
        if (find(addr, channel) != nullptr)
            return;

        std::unique_ptr<rudp_endpoint_t> endpoint = std::make_unique<rudp_endpoint_t>();
        auto pcb = ikcp_create(channel, endpoint.get());
        endpoint->ikcp = pcb;
        endpoint->inactive_timeout = inactive_timeout;
        endpoint->remote_address = addr;
        endpoint->impl = this;
        endpoint->timer_reg.id = -1;
        endpoint->channel = channel;
        endpoint->wait_for_io = false;

        ikcp_setoutput(pcb, udp_output);
        ikcp_wndsize(pcb, 128, 128);
        auto ptr = endpoint.get();
        /// XXX: 避免绑定到不同线程，使用同一个loop，意味着无法在多个线程间负载均衡，尝试异步读取recv to kcp
        /// 可能解决问题
        endpoint->econtext.set_loop(socket->get_loop());

        if (co_func)
        {
            endpoint->econtext.run([this, ptr, co_func]() {
                rudp_connection_t conn;
                conn.address = ptr->remote_address;
                conn.channel = ptr->channel;
                co_func(conn);
                remove_connection(ptr->remote_address, ptr->channel);
            });
        }
        else
        {
            endpoint->econtext.run([this, ptr]() {
                rudp_connection_t conn;
                conn.address = ptr->remote_address;
                conn.channel = ptr->channel;
                if (new_connection_handler)
                    new_connection_handler(conn);
                remove_connection(ptr->remote_address, ptr->channel);
            });
        }

        user_map[addr].emplace(channel, std::move(endpoint));
        // fast mode
        rudp_connection_t conn;
        conn.address = addr;
        conn.channel = channel;
        config(conn, 1);
    }

    void set_wndsize(socket_addr_t addr, int channel, int send, int recv)
    {
        auto endpoint = find(addr, channel);
        if (endpoint == nullptr)
            return;
        ikcp_wndsize(endpoint->ikcp, send, recv);
    }

    bool removeable(socket_addr_t addr, int channel)
    {
        auto endpoint = find(addr, channel);
        if (endpoint == nullptr)
            return false;

        return ikcp_waitsnd(endpoint->ikcp) == 0;
    }

    void remove_connection(socket_addr_t addr, int channel)
    {
        auto endpoint = find(addr, channel);
        if (endpoint == nullptr)
            return;
        auto ikcp = endpoint->ikcp;
        aclose_connection(endpoint);
        ikcp_release(ikcp);
    }

    co::async_result_t<io_result> awrite(co::paramter_t &param, rudp_connection_t conn, socket_buffer_t &buffer)
    {
        assert(buffer.get_length() <= INT32_MAX);
        auto endpoint = find(conn);
        if (endpoint == nullptr)
            return io_result::failed;
        if (param.is_stop())
        {
            endpoint->wait_for_io = false;
            buffer.finish_walk();
            return io_result::timeout;
        }
        auto pcb = endpoint->ikcp;
        if (ikcp_send(pcb, (const char *)buffer.get(), buffer.get_length()) >= 0) // wnd full, wait...
        {
            set_timer(endpoint);
            endpoint->wait_for_io = false;
            buffer.finish_walk();
            return io_result::ok;
        }
        endpoint->wait_for_io = true;
        return {};
    }

    void rudp_server_main()
    {
        socket_addr_t target;
        while (1)
        {
            recv_buffer.expect().origin_length();
            if (co::await(socket_aread_from, socket, recv_buffer, target) != io_result::ok)
            {
                socket->sleep(1000);
                continue;
            }
            auto it = user_map.find(target);
            if (it == user_map.end())
            {
                if (unknown_handler)
                {
                    if (!unknown_handler(target))
                    {
                        // discard packet
                        continue;
                    }
                    it = user_map.find(target);
                    if (it == user_map.end())
                    {
                        // discard packet !
                        continue;
                    }
                }
            }
            int conv = ikcp_getconv(recv_buffer.get());
            auto it2 = it->second.find(conv);
            if (it2 == it->second.end())
                continue; // this channel isn't exist. ignore
            auto endpoint = it2->second.get();
            endpoint->last_alive = get_current_time();
            // udp -> ikcp
            ikcp_input(endpoint->ikcp, (char *)recv_buffer.get(), recv_buffer.get_length());
            if (ikcp_peeksize(endpoint->ikcp) >= 0)
            {
                if (endpoint->wait_for_io)
                    endpoint->econtext.start();
            }

            recv_buffer.expect().origin_length();
        }
    }

    co::async_result_t<io_result> aread(co::paramter_t &param, rudp_connection_t conn, socket_buffer_t &buffer)
    {
        auto endpoint = find(conn);
        if (endpoint == nullptr)
            return io_result::failed;

        if (param.is_stop()) /// stop timeout
        {
            buffer.finish_walk();
            endpoint->wait_for_io = false;
            return io_result::timeout;
        }

        // data <- KCP <- UDP
        auto len = ikcp_recv(endpoint->ikcp, (char *)buffer.get(), buffer.get_length());
        if (len >= 0)
        {
            set_timer(endpoint);
            buffer.walk_step(len);
            buffer.finish_walk();
            endpoint->wait_for_io = false;
            return io_result::ok;
        }
        endpoint->wait_for_io = true;
        return {};
    }

    socket_t *get_socket() const { return socket; }

    void aclose_connection(rudp_endpoint_t *endpoint)
    {
        while (1)
        {
            if (ikcp_waitsnd(endpoint->ikcp) <= 0)
                break;
            endpoint->wait_for_io = true;
            endpoint->econtext.stop();
        }
        if (endpoint->timer_reg.id >= 0)
        {
            endpoint->econtext.get_loop()->remove_timer(endpoint->timer_reg);
            endpoint->timer_reg.id = -1;
        }

        auto it = user_map.find(endpoint->remote_address);
        if (it != user_map.end())
        {
            auto it2 = it->second.find(endpoint->channel);
            if (it2 != it->second.end())
            {
                it->second.erase(it2);
            }
            if (it->second.empty())
            {
                user_map.erase(it);
            }
        }
    }

    void close_all_peer()
    {
        auto ts = (get_current_time() - base_time) / 1000;
        for (auto &it : user_map)
        {
            for (auto &it2 : it.second)
            {
                /// don't wait send buffer
                ikcp_release(it2.second->ikcp);
            }
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

int udp_output(const char *buf, int len, ikcpcb *kcp, void *user)
{
    rudp_endpoint_t *endpoint = (rudp_endpoint_t *)user;
    socket_buffer_t buffer((byte *)(buf), len);
    buffer.expect().origin_length();
    endpoint->last_alive = get_current_time();
    // output data to kernel, sendto udp will return immediately forever.
    // so there is no need to switch to socket coroutine.
    co::await(socket_awrite_to, endpoint->impl->socket, buffer, endpoint->remote_address);
    // send failed when kernel buffer is full.
    // KCP will not receive this package's ACK.
    // trigger resend after next tick
    return 0;
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

void rudp_t::add_connection(socket_addr_t addr, int channel, microsecond_t inactive_timeout)
{
    impl->add_connection(addr, channel, inactive_timeout, std::function<void(rudp_connection_t)>());
}

void rudp_t::add_connection(socket_addr_t addr, int channel, microsecond_t inactive_timeout,
                            std::function<void(rudp_connection_t)> co_func)
{
    impl->add_connection(addr, channel, inactive_timeout, co_func);
}

void rudp_t::config(rudp_connection_t conn, int level) { impl->config(conn, level); }

void rudp_t::set_wndsize(socket_addr_t addr, int channel, int send, int recv)
{
    impl->set_wndsize(addr, channel, send, recv);
}

rudp_t &rudp_t::on_new_connection(new_connection_handler_t handler)
{
    impl->on_new_connection(handler);
    return *this;
}

bool rudp_t::removeable(socket_addr_t addr, int channel) { return impl->removeable(addr, channel); }

void rudp_t::remove_connection(socket_addr_t addr, int channel) { impl->remove_connection(addr, channel); }

void rudp_t::remove_connection(rudp_connection_t conn) { impl->remove_connection(conn.address, conn.channel); }

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

socket_t *rudp_t::get_socket() const { return impl->get_socket(); }

void rudp_t::run_at(rudp_connection_t conn, std::function<void()> func) { impl->run_at(conn, func); }

void rudp_t::close_all_remote() { impl->close_all_peer(); }

void rudp_t::close() { impl->close(); }

/// wrappers -----------------------

co::async_result_t<io_result> rudp_t::awrite(co::paramter_t &param, rudp_connection_t conn, socket_buffer_t &buffer)
{
    return impl->awrite(param, conn, buffer);
}

co::async_result_t<io_result> rudp_t::aread(co::paramter_t &param, rudp_connection_t conn, socket_buffer_t &buffer)
{
    return impl->aread(param, conn, buffer);
}

co::async_result_t<io_result> rudp_awrite(co::paramter_t &param, rudp_t *rudp, rudp_connection_t conn,
                                          socket_buffer_t &buffer)
{
    return rudp->awrite(param, conn, buffer);
}

co::async_result_t<io_result> rudp_aread(co::paramter_t &param, rudp_t *rudp, rudp_connection_t conn,
                                         socket_buffer_t &buffer)
{
    return rudp->aread(param, conn, buffer);
}

} // namespace net
