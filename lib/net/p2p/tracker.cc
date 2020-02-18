#include "net/p2p/tracker.hpp"
#include "net/socket.hpp"
#include <algorithm>
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
    return co::await(tcp::conn_awrite_packet, conn, head, buffer);
}

void tracker_server_t::update_tracker(socket_addr_t addr, tcp::connection_t conn, tracker_ping_pong_t &res)
{
    auto it = trackers.find(addr);
    if (it == trackers.end())
    {
        it = trackers.insert(std::make_pair(addr, std::make_unique<tracker_info_t>())).first;
        auto &pnode = *it->second.get();
        pnode.is_client = false;
        pnode.node.ip = addr.v4_addr();
        pnode.node.port = res.port;
    }
    auto &pnode = *it->second.get();

    pnode.workload = res.peer_workload;
    pnode.trackers = res.tracker_neighbor_count;
    pnode.conn_server = conn;
    pnode.last_ping = get_current_time();
}

void tracker_server_t::server_main(tcp::connection_t conn)
{
    tcp::package_head_t head;
    while (1)
    {
        if (co::await(tcp::conn_aread_packet_head, conn, head) != io_result::ok)
            return;
        if (head.version != 4)
            return;

        if (head.v4.msg_type == tracker_packet::ping)
        {
            int len = head.v4.size;
            if (len != sizeof(tracker_ping_pong_t)) // TL;DR
                return;
            socket_buffer_t buffer(len);
            buffer.expect().origin_length();
            if (co::await(tcp::conn_aread_packet_content, conn, buffer) != io_result::ok)
                return;
            tracker_ping_pong_t ping;
            assert(endian::cast_to(buffer, ping));
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
            if (len != sizeof(get_nodes_request_t))
                return;

            socket_buffer_t buffer(len);
            buffer.expect().origin_length();
            if (co::await(tcp::conn_aread_packet_content, conn, buffer) != io_result::ok)
                return;
            assert(endian::cast_to(buffer, request));
            request.max_count = std::min((int)request.max_count, 500);
            head.version = 4;
            head.v4.msg_type = tracker_packet::get_nodes_respond;
            std::unique_ptr<char[]> data =
                std::make_unique<char[]>(sizeof(get_nodes_respond_t) + request.max_count * sizeof(peer_node_t));
            get_nodes_respond_t *respond = (get_nodes_respond_t *)data.get();
            // use buffer from 'data'
            socket_buffer_t send_buffer((byte *)data.get(),
                                        sizeof(get_nodes_respond_t) + request.max_count * sizeof(peer_node_t));
            send_buffer.expect().origin_length();
            // skip head
            send_buffer.walk_step(sizeof(get_nodes_respond_t));
            int i = 0;
            if (request.strategy == request_strategy::random)
            {
                if (node_infos.size() > 0)
                {
                    int start = rand() % node_infos.size();
                    for (int j = start + 1; i < request.max_count; j++)
                    {
                        if (j >= node_infos.size())
                            j = 0;
                        if (j == start)
                            break;
                        if (get_current_time() - node_infos[j].last_ping <= node_tick_times * node_tick_timespan)
                        {
                            // add
                            respond->peers[i] = node_infos[j].node;
                            assert(endian::cast_inplace(respond->peers[i++], send_buffer));
                            send_buffer.walk_step(sizeof(peer_node_t));
                        }
                    }
                }
            }
            else if (request.strategy == request_strategy::min_workload)
            {
                /// TODO: sort by workload and return to client
            }
            else
            {
                // close it
                return;
            }

            // set head
            respond->sid = request.sid;
            respond->available_count = node_infos.size();
            respond->return_count = i;

            send_buffer.expect().origin_length();
            assert(endian::cast_inplace(*respond, send_buffer));
            send_buffer.expect().length(sizeof(get_nodes_respond_t) + i * sizeof(peer_node_t));

            co::await(tcp::conn_awrite_packet, conn, head, send_buffer);
        }
        else if (head.v4.msg_type == tracker_packet::get_trackers_request)
        {
            get_trackers_request_t request;
            int len = head.v4.size;
            if (len != sizeof(get_trackers_request_t))
                return;

            socket_buffer_t buffer((byte *)&request, len);
            buffer.expect().origin_length();
            if (co::await(tcp::conn_aread_packet_content, conn, buffer) != io_result::ok)
                return;

            assert(endian::cast_inplace(request, buffer));

            request.max_count = std::min(500, (int)request.max_count);
            // set respond header
            head.version = 4;
            head.v4.msg_type = tracker_packet::get_trackers_respond;
            std::unique_ptr<char[]> data =
                std::make_unique<char[]>(sizeof(get_trackers_respond_t) + request.max_count * sizeof(tracker_node_t));
            get_trackers_respond_t *respond = (get_trackers_respond_t *)data.get();
            socket_buffer_t send_buffer((byte *)data.get(),
                                        sizeof(get_trackers_respond_t) + request.max_count * sizeof(tracker_node_t));
            send_buffer.expect().origin_length();
            // skip head first
            send_buffer.walk_step(sizeof(get_trackers_respond_t));
            /// ignore strategy
            /// fill data
            int i = 0;
            for (auto it = trackers.begin(); it != trackers.end();)
            {
                if (it->second->closed)
                {
                    it = trackers.erase(it);
                    continue;
                }
                auto node = it->second->node;
                assert(endian::save_to(node, send_buffer));
                send_buffer.walk_step(sizeof(tracker_node_t));
                i++;
                ++it;
            }
            // set head
            respond->available_count = trackers.size();
            respond->return_count = i;

            send_buffer.expect().origin_length();
            assert(endian::cast_inplace(*respond, send_buffer));
            send_buffer.expect().length(sizeof(get_trackers_respond_t) + i * sizeof(tracker_node_t));
            // send
            co::await(tcp::conn_awrite_packet, conn, head, send_buffer);
        }
        else if (head.v4.msg_type == tracker_packet::heartbeat)
        {
            // there is no data to recv
            if (head.v4.size != 0)
                return;
            auto addr = conn.get_socket()->remote_addr();
            auto it = nodes.find(addr);
            if (it == nodes.end())
            {
                // new node join
                int index = node_infos.size();
                it = nodes.insert(std::make_pair(addr, index)).first;
                node_infos.emplace_back();
                auto &node = node_infos[index];
                node.node.ip = addr.v4_addr();
                node.node.port = addr.get_port();
            }
            auto index = it->second;
            auto &node = node_infos[index];
            node.last_ping = get_current_time();
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
    if (co::await(tcp::conn_aread_packet_head, conn, head) != io_result::ok)
        return;
    if (head.version != 4)
        return;
    if (head.v4.msg_type == tracker_packet::pong) // pong
    {
        auto len = head.v4.size;
        if (len != sizeof(tracker_ping_pong_t)) // TL.DR
            return;

        socket_buffer_t buffer(len);
        buffer.expect().origin_length();
        if (co::await(tcp::conn_aread_packet_content, conn, buffer) != io_result::ok)
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
    server.on_client_join(std::bind(&tracker_server_t::server_main, this, std::placeholders::_2));
    server.on_client_error([this](tcp::server_t &, socket_t *so, connection_state state) {
        if (error_handler)
            error_handler(*this, so, state);
    });
    server.listen(context, addr, 10000000, reuse_addr);
}

void tracker_server_t::link_other_tracker_server(event_context_t &context, socket_addr_t addr, microsecond_t timeout)
{
    auto it = trackers.find(addr);
    if (it == trackers.end())
    {
        it = trackers.insert(std::make_pair(addr, std::make_unique<tracker_info_t>())).first;

        auto &p = *it->second.get();
        p.is_client = true;
        p.node.ip = addr.v4_addr();
        p.node.port = addr.get_port();
        p.client.on_server_connect(std::bind(&tracker_server_t::client_main, this, std::placeholders::_2));
        p.client.on_server_error([addr, this](tcp::client_t &, socket_t *so, connection_state state) {
            auto it = trackers.find(addr);
            if (it != trackers.end())
                it->second->closed = true;
        });

        p.client.connect(context, addr, timeout);
    }
}

tracker_server_t &tracker_server_t::on_link_error(error_handler_t handler)
{
    error_handler = handler;
    return *this;
}

std::vector<tracker_node_t> tracker_server_t::get_trackers() const
{
    std::vector<tracker_node_t> vec;
    auto cur = get_current_time();
    for (const auto &it : trackers)
    {
        auto &pnode = *it.second.get();
        if (cur - pnode.last_ping < tick_timespan * tick_times)
            vec.emplace_back(pnode.node);
    }
    return vec;
}

/// client

void heartbeat(tcp::connection_t conn)
{
    tcp::package_head_t head;
    head.version = 4;
    head.v4.msg_type = tracker_packet::heartbeat;
    int buf;
    socket_buffer_t buffer((byte *)&buf, 0);
    buffer.expect().length(0);
    co::await(tcp::conn_awrite_packet, conn, head, buffer);
}

void tracker_node_client_t::main(tcp::connection_t conn)
{
    /// first send heartbeat
    heartbeat(conn);
    while (1)
    {
        if (request_trackers)
        {
            update_trackers(50, request_strategy::min_workload);
            request_trackers = false;
        }
        if (request_nodes)
        {
            update_nodes();
            request_nodes = false;
        }
        tcp::package_head_t head;
        wait_next_package = true;
        auto ret = co::await_timeout(node_tick_timespan, tcp::conn_aread_packet_head, conn, head);
        wait_next_package = false;
        if (ret == io_result::timeout)
        {
            heartbeat(conn);
            continue;
        }
        if (head.version != 4)
            return;
        if (head.v4.msg_type == tracker_packet::get_nodes_respond)
        {
            if (head.v4.size < sizeof(get_nodes_respond_t))
                return;
            get_nodes_respond_t respond;
            // buffer in stack
            socket_buffer_t recv_buffer((byte *)&respond, sizeof(respond));
            recv_buffer.expect().origin_length();
            co::await(tcp::conn_aread_packet_content, conn, recv_buffer);
            assert(endian::cast_inplace(respond, recv_buffer));
            if (respond.return_count > 1000)
                return;
            /// get rest node
            std::unique_ptr<char[]> data = std::make_unique<char[]>(respond.return_count * sizeof(peer_node_t));
            socket_buffer_t recv_data_buffer((byte *)data.get(), respond.return_count * sizeof(peer_node_t));
            recv_data_buffer.expect().origin_length();
            co::await(tcp::conn_aread_packet_content, conn, recv_data_buffer);
            peer_node_t *node = (peer_node_t *)data.get();
            for (auto i = 0; i < respond.return_count; i++)
            {
                endian::cast_inplace(node[i], recv_data_buffer);
                recv_data_buffer.walk_step(sizeof(peer_node_t));
            }
            if (node_update_handler)
                node_update_handler(*this, node, (u64)respond.return_count);
        }
        else if (head.v4.msg_type == tracker_packet::get_trackers_respond)
        {
            if (head.v4.size < sizeof(get_trackers_respond_t))
                return;
            get_trackers_respond_t respond;
            // buffer in stack
            socket_buffer_t recv_buffer((byte *)&respond, sizeof(respond));
            recv_buffer.expect().origin_length();
            co::await(tcp::conn_aread_packet_content, conn, recv_buffer);
            assert(endian::cast_inplace(respond, recv_buffer));
            if (respond.return_count > 1000)
                return;
            /// get rest node
            std::unique_ptr<char[]> data = std::make_unique<char[]>(respond.return_count * sizeof(tracker_node_t));
            socket_buffer_t recv_data_buffer((byte *)data.get(), respond.return_count * sizeof(tracker_node_t));
            recv_data_buffer.expect().origin_length();
            co::await(tcp::conn_aread_packet_content, conn, recv_data_buffer);
            tracker_node_t *node = (tracker_node_t *)data.get();
            for (auto i = 0; i < respond.return_count; i++)
            {
                endian::cast_inplace(node[i], recv_data_buffer);
                recv_data_buffer.walk_step(sizeof(tracker_node_t));
            }
            if (tracker_update_handler)
                tracker_update_handler(*this, node, (u64)respond.return_count);
        }
        else
        {
            return;
        }
    }
}

void tracker_node_client_t::config(u64 sid, int max_request_count, request_strategy strategy)
{
    this->sid = sid;
    this->request_count = max_request_count;
    this->strategy = strategy;
}

void tracker_node_client_t::connect_server(event_context_t &context, socket_addr_t addr, microsecond_t timeout)
{
    wait_next_package = false;
    client.on_server_connect(std::bind(&tracker_node_client_t::main, this, std::placeholders::_2))
        .on_server_error([this](tcp::client_t &, socket_t *so, connection_state state) {
            if (error_handler)
                error_handler(*this, so, state);
        });

    client.connect(context, addr, timeout);
}

void tracker_node_client_t::update_nodes()
{
    tcp::package_head_t head;
    head.version = 4;
    head.v4.msg_type = tracker_packet::get_nodes_request;
    get_nodes_request_t request;
    request.sid = sid;
    request.strategy = strategy;
    request.max_count = request_count;
    socket_buffer_t buffer(sizeof(get_nodes_request_t));
    buffer.expect().origin_length();
    assert(endian::save_to(request, buffer));
    tcp::connection_t conn = client.get_connection();
    co::await(tcp::conn_awrite_packet, conn, head, buffer);
}

void tracker_node_client_t::update_trackers(int count, request_strategy strategy)
{
    tcp::package_head_t head;
    head.version = 4;
    head.v4.msg_type = tracker_packet::get_trackers_request;
    get_trackers_request_t request;
    request.strategy = strategy;
    request.max_count = count;
    socket_buffer_t buffer(sizeof(get_trackers_request_t));
    buffer.expect().origin_length();
    assert(endian::save_to(request, buffer));
    tcp::connection_t conn = client.get_connection();
    co::await(tcp::conn_awrite_packet, conn, head, buffer);
}

void tracker_node_client_t::request_update_trackers()
{
    if (client.is_connect())
    {
        if (wait_next_package)
        {
            client.get_socket()->get_coroutine()->resume_with(
                [this]() { update_trackers(100, request_strategy::min_workload); });
        }
        return;
    }
    request_trackers = true;
}

void tracker_node_client_t::request_update_nodes()
{
    /// XXX: there is not thead-safety
    if (client.is_connect())
    {
        if (wait_next_package)
        {
            client.get_socket()->get_coroutine()->resume_with([this]() { update_nodes(); });
        }
        return;
    }
    request_nodes = true;
}

tracker_node_client_t &tracker_node_client_t::on_nodes_update(nodes_update_handler_t handler)
{
    node_update_handler = handler;
    return *this;
}

tracker_node_client_t &tracker_node_client_t::on_trackers_update(trackers_update_handler_t handler)
{
    tracker_update_handler = handler;
    return *this;
}

tracker_node_client_t &tracker_node_client_t::on_error(error_handler_t handler)
{
    error_handler = handler;
    return *this;
}

} // namespace net::p2p
