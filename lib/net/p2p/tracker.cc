#include "net/p2p/tracker.hpp"
#include "net/socket.hpp"
#include <random>

namespace net::p2p
{

io_result do_ping_pong(tcp::connection_t conn, u32 ip, u16 port, u32 workload, u32 trackers, int code)
{
    endian::cast(port); // cast to network endian
    endian::cast(ip);   // cast to network endian

    tcp::package_head_t head;
    head.version = 4;
    head.v4.msg_type = code;
    tracker_ping_pong_t pong;
    pong.ip = ip;
    pong.port = port;
    pong.peer_workload = workload;
    pong.tracker_neighbor_count = trackers;
    socket_buffer_t buffer(sizeof(pong));
    buffer.expect().origin_length();
    assert(endian::save_to(pong, buffer));
    return co::await(tcp::connection_awrite_package, conn, head, buffer);
}

void tracker_server_t::update_tracker(socket_addr_t addr, tcp::connection_t conn, tracker_ping_pong_t &res)
{
    auto it = trackers.find(addr);
    if (it == trackers.end())
    {
        it = trackers.insert(std::make_pair(addr, std::make_unique<tracker_info_t>())).first;
        it->second->is_client = false;
        it->second->address = addr;
    }
    it->second->workload = res.peer_workload;
    it->second->trackers = res.tracker_neighbor_count;
    it->second->conn_server = conn;
    it->second->last_ping = get_current_time();
}

void tracker_server_t::server_main(tcp::connection_t conn)
{
    tcp::package_head_t head;
    while (1)
    {
        if (co::await(tcp::connection_aread_package_head, conn, head) != io_result::ok)
            return;
        if (head.version != 4)
            return;

        if (head.v4.msg_type == tracker_packet::ping)
        {
            int len = head.v4.size;
            if (len != sizeof(tracker_ping_pong_t)) // TL.DR
                return;
            socket_buffer_t buffer(len);
            buffer.expect().origin_length();
            if (co::await(tcp::connection_aread_package_content, conn, buffer) != io_result::ok)
                return;
            tracker_ping_pong_t ping;
            assert(endian::cast_to(buffer, ping));
            /// FIXME: this remote address is not real, it is client random port
            auto remote_addr = conn.get_socket()->remote_addr();
            endian::cast(ping.ip);
            endian::cast(ping.port);

            // save
            update_tracker(remote_addr, conn, ping);

            do_ping_pong(conn, get_ip(server.get_socket()).v4_addr(), server.get_socket()->local_addr().get_port(),
                         nodes.size(), trackers.size(), tracker_packet::pong);
            // close it
            return;
        }
        else if (head.v4.msg_type == tracker_packet::get_nodes_request)
        {
            get_nodes_request_t request;
            int len = head.v4.size;
            if (len != sizeof(get_nodes_request_t)) // TL.DR
                return;

            socket_buffer_t buffer(len);
            buffer.expect().origin_length();
            if (co::await(tcp::connection_aread_package_content, conn, buffer) != io_result::ok)
                return;
            assert(endian::cast_to(buffer, request));
            if (request.strategy == request_strategy::random)
            {
            }
        }
        else if (head.v4.msg_type == tracker_packet::get_trackers_request)
        {
            continue;
        }
        else if (head.v4.msg_type == tracker_packet::heartbeat)
        {
            continue;
        }
        else
        {
            // else close it
            return;
        }
    }
}

void tracker_server_t::client_main(tcp::connection_t conn)
{
    do_ping_pong(conn, get_ip(server.get_socket()).v4_addr(), server.get_socket()->local_addr().get_port(),
                 nodes.size(), trackers.size(), 1);

    tcp::package_head_t head;
    if (co::await(tcp::connection_aread_package_head, conn, head) != io_result::ok)
        return;
    if (head.version != 4)
        return;
    if (head.v4.msg_type == tracker_packet::pong) // pong
    {
        int len = head.v4.size;
        if (len != sizeof(tracker_ping_pong_t)) // TL.DR
            return;
        socket_buffer_t buffer(len);
        buffer.expect().origin_length();
        if (co::await(tcp::connection_aread_package_content, conn, buffer) != io_result::ok)
            return;
        tracker_ping_pong_t pong;
        assert(endian::cast_to(buffer, pong));
        auto remote_addr = conn.get_socket()->remote_addr();
        endian::cast(pong.ip);
        endian::cast(pong.port);
        // save
        update_tracker(remote_addr, conn, pong);
    }
}

void tracker_server_t::bind(event_context_t &context, socket_addr_t addr, bool reuse_addr)
{
    server.at_client_join(std::bind(&tracker_server_t::server_main, this, std::placeholders::_2));
    server.listen(context, addr, 10000000, reuse_addr);
}

void tracker_server_t::link_other_tracker_server(event_context_t &context, socket_addr_t addr, microsecond_t timeout)
{
    auto it = trackers.find(addr);
    if (it == trackers.end())
    {
        it = trackers.insert(std::make_pair(addr, std::make_unique<tracker_info_t>())).first;
        auto ptr = it->second.get();
        ptr->is_client = true;
        ptr->address = addr;
        ptr->client.at_server_connect(std::bind(&tracker_server_t::client_main, this, std::placeholders::_2));
        ptr->client.at_server_connection_error([ptr, this](tcp::client_t &, socket_t *so, connection_state state) {
            auto it = trackers.find(ptr->address);
            if (it != trackers.end())
            {
                trackers.erase(it);
            }
        });

        ptr->client.connect(context, addr, timeout);
    }
}

std::vector<socket_addr_t> tracker_server_t::get_trackers() const
{
    std::vector<socket_addr_t> vec;
    auto cur = get_current_time();
    for (const auto &it : trackers)
    {
        if (cur - it.second->last_ping < tick_timespan * tick_times)
            vec.emplace_back(it.second->address);
    }
    return vec;
}

void tracker_node_client_t::connect_server(event_context_t &context, socket_addr_t addr, microsecond_t timeout)
{
    client.at_server_connect([this](tcp::client_t &, tcp::connection_t conn) {

    });
    client.connect(context, addr, timeout);
}

tracker_node_client_t &tracker_node_client_t::at_nodes_update(at_nodes_update_handler_t handler)
{
    update_handler = handler;
    return *this;
}

tracker_node_client_t &tracker_node_client_t::at_trackers_update(at_trackers_update_handler_t handler)
{
    tracker_update_handler = handler;
    return *this;
}

} // namespace net::p2p
