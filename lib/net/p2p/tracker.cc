#include "net/p2p/tracker.hpp"
#include "net/socket.hpp"
#include <algorithm>
#include <random>

namespace net::p2p
{

io_result do_ping_pong(tcp::connection_t conn, u16 port, u16 udp_port, u32 workload, u32 trackers, int code)
{
    endian::cast(port); // cast to network endian

    tcp::package_head_t head;
    head.version = 4;
    head.v4.msg_type = code;
    tracker_ping_pong_t pong;
    pong.port = port;
    pong.udp_port = udp_port;
    pong.peer_workload = workload;
    pong.tracker_neighbor_count = trackers;
    socket_buffer_t buffer = socket_buffer_t::from_struct(pong);
    buffer.expect().origin_length();
    endian::cast_inplace(pong, buffer);
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
        pnode.node.udp_port = res.udp_port;
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
    auto remote_addr = conn.get_socket()->remote_addr();
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
            tracker_ping_pong_t ping;
            socket_buffer_t buffer = socket_buffer_t::from_struct(ping);
            buffer.expect().origin_length();
            if (co::await(tcp::conn_aread_packet_content, conn, buffer) != io_result::ok)
                return;
            endian::cast_inplace(ping, buffer);

            endian::cast(ping.port);
            endian::cast(ping.udp_port);

            // save
            update_tracker(remote_addr, conn, ping);

            do_ping_pong(conn, server.get_socket()->local_addr().get_port(), udp.get_socket()->local_addr().get_port(),
                         nodes.size(), trackers.size(), tracker_packet::pong);
        }
        else if (head.v4.msg_type == tracker_packet::get_tracker_info_request)
        {
            if (head.v4.size != 0)
                return;
            get_tracker_info_respond_t respond;
            socket_buffer_t buffer = socket_buffer_t::from_struct(respond);
            buffer.expect().origin_length();
            respond.tudp_port = udp_port; // server udp port
            /// get address NAT
            respond.cip = conn.get_socket()->remote_addr().v4_addr();
            respond.cport = conn.get_socket()->remote_addr().get_port();

            endian::cast_inplace(respond, buffer);
            buffer.expect().origin_length();
            head.version = 4;
            head.v4.msg_type = tracker_packet::get_tracker_info_respond;

            co::await(tcp::conn_awrite_packet, conn, head, buffer);
        }
        else if (head.v4.msg_type == tracker_packet::get_nodes_request)
        {
            get_nodes_request_t request;
            int len = head.v4.size;
            if (len != sizeof(get_nodes_request_t))
                return;

            socket_buffer_t buffer = socket_buffer_t::from_struct(request);
            buffer.expect().origin_length();
            if (co::await(tcp::conn_aread_packet_content, conn, buffer) != io_result::ok)
                return;
            endian::cast_inplace(request, buffer);

            request.max_count = std::min((int)request.max_count, 500);
            head.version = 4;
            head.v4.msg_type = tracker_packet::get_nodes_respond;
            std::unique_ptr<char[]> data =
                std::make_unique<char[]>(sizeof(get_nodes_respond_t) + request.max_count * sizeof(tracker_peer_node_t));
            get_nodes_respond_t *respond = (get_nodes_respond_t *)data.get();
            // use buffer from 'data'
            socket_buffer_t send_buffer((byte *)data.get(),
                                        sizeof(get_nodes_respond_t) + request.max_count * sizeof(tracker_peer_node_t));
            send_buffer.expect().origin_length();
            // skip head
            send_buffer.walk_step(sizeof(get_nodes_respond_t));
            int i = 0;

            auto cur_time = get_current_time();
            if (request.strategy == request_strategy::random)
            {
                if (node_infos.size() > 0)
                {
                    int start = rand() % node_infos.size();
                    bool over = false;
                    for (int j = start; i < request.max_count; j++)
                    {
                        if (j >= node_infos.size())
                        {
                            over = true;
                            j = 0;
                        }
                        if (j == start && over)
                            break;

                        if (request.sid == node_infos[j].sid &&
                            cur_time - node_infos[j].last_ping <= node_tick_times * node_tick_timespan)
                        {
                            // add
                            respond->peers[i].ip = node_infos[j].node.ip;
                            respond->peers[i].port = node_infos[j].node.port;
                            respond->peers[i].udp_port = node_infos[j].node.udp_port;

                            endian::cast_inplace(respond->peers[i++], send_buffer);
                            send_buffer.walk_step(sizeof(tracker_peer_node_t));
                        }
                    }
                }
            }
            else if (request.strategy == request_strategy::min_workload)
            {
                /// TODO: sort by workload and return to client
            }
            else if (request.strategy == request_strategy::edge_node)
            {
                if (node_infos.size() > 0)
                {
                    int start = rand() % node_infos.size();
                    bool over = false;
                    for (int j = start; i < request.max_count; j++)
                    {
                        if (j >= node_infos.size())
                        {
                            over = true;
                            j = 0;
                        }
                        if (j == start && over)
                            break;

                        if (node_infos[j].sid == 0 &&
                            cur_time - node_infos[j].last_ping <= node_tick_times * node_tick_timespan)
                        {
                            // add
                            respond->peers[i].ip = node_infos[j].node.ip;
                            respond->peers[i].port = node_infos[j].node.port;
                            respond->peers[i].udp_port = node_infos[j].node.udp_port;

                            endian::cast_inplace(respond->peers[i++], send_buffer);
                            send_buffer.walk_step(sizeof(tracker_peer_node_t));
                        }
                    }
                }
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
            endian::cast_inplace(*respond, send_buffer);
            send_buffer.expect().length(sizeof(get_nodes_respond_t) + i * sizeof(tracker_peer_node_t));

            co::await(tcp::conn_awrite_packet, conn, head, send_buffer);
        }
        else if (head.v4.msg_type == tracker_packet::get_trackers_request)
        {
            get_trackers_request_t request;
            int len = head.v4.size;
            if (len != sizeof(get_trackers_request_t))
                return;

            socket_buffer_t buffer = socket_buffer_t::from_struct(request);
            buffer.expect().origin_length();
            if (co::await(tcp::conn_aread_packet_content, conn, buffer) != io_result::ok)
                return;

            endian::cast_inplace(request, buffer);

            request.max_count = std::min(500, (int)request.max_count);
            // set respond header
            head.version = 4;
            head.v4.msg_type = tracker_packet::get_trackers_respond;
            std::unique_ptr<char[]> data = std::make_unique<char[]>(sizeof(get_trackers_respond_t) +
                                                                    request.max_count * sizeof(tracker_tracker_node_t));
            get_trackers_respond_t *respond = (get_trackers_respond_t *)data.get();
            socket_buffer_t send_buffer((byte *)data.get(), sizeof(get_trackers_respond_t) +
                                                                request.max_count * sizeof(tracker_tracker_node_t));
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
                tracker_tracker_node_t tnode;
                tnode.port = node.port;
                tnode.udp_port = node.udp_port;
                tnode.ip = node.ip;
                endian::save_to(tnode, send_buffer);
                send_buffer.walk_step(sizeof(tracker_tracker_node_t));
                i++;
                ++it;
            }
            // set head
            respond->available_count = trackers.size();
            respond->return_count = i;

            send_buffer.expect().origin_length();
            endian::cast_inplace(*respond, send_buffer);
            send_buffer.expect().length(sizeof(get_trackers_respond_t) + i * sizeof(tracker_tracker_node_t));
            // send
            co::await(tcp::conn_awrite_packet, conn, head, send_buffer);
        }
        else if (head.v4.msg_type == tracker_packet::init_connection)
        {
            if (head.v4.size != sizeof(init_connection_t))
                return;
            init_connection_t init;
            socket_buffer_t buffer = socket_buffer_t::from_struct(init);
            buffer.expect().origin_length();
            co::await(tcp::conn_aread_packet_content, conn, buffer);
            endian::cast_inplace(init, buffer);
            if (init.register_sid == 0) // if sid == 0. it is edge server
            {
                // check edge key
                for (int i = 0; i < sizeof(init.key) && i < edge_key.size(); i++)
                {
                    if (edge_key[i] != init.key[i])
                    {
                        if (peer_error_handler)
                        {
                            peer_error_handler(*this, remote_addr, init.register_sid,
                                               connection_state::secure_check_failed);
                        }
                        return;
                    }
                }
            }
            auto it = nodes.find(remote_addr);
            if (it == nodes.end())
            {
                // new node join
                int index = node_infos.size();
                it = nodes.insert(std::make_pair(remote_addr, index)).first;
                node_infos.emplace_back();
                auto &node = node_infos[index];
                node.node.ip = remote_addr.v4_addr();
                node.node.port = remote_addr.get_port();
                node.node.udp_port = 0;
                node.conn = conn;
            }
            auto index = it->second;
            auto &node = node_infos[index];
            node.last_ping = get_current_time();
            node.sid = init.register_sid;
            if (add_handler)
                add_handler(*this, node.node, node.sid);
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
                continue;
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
    auto remote_addr = conn.get_socket()->remote_addr();

    while (1)
    {
        do_ping_pong(conn, server.get_socket()->local_addr().get_port(), udp.get_socket()->local_addr().get_port(),
                     nodes.size(), trackers.size(), tracker_packet::ping);

        tcp::package_head_t head;
        auto ret = co::await_timeout(tick_timespan, tcp::conn_aread_packet_head, conn, head);
        if (ret == io_result::timeout)
        {
            auto it = trackers.find(remote_addr);
            if (it != trackers.end())
            {
                /// check if peer pingpong is timeout
                if (get_current_time() - it->second->last_ping > tick_timespan * tick_times)
                {
                    if (link_error_handler)
                        link_error_handler(*this, remote_addr, connection_state::timeout);
                    // close connection
                    return;
                }
            }
        }
        else if (ret != io_result::ok)
        {
            if (link_error_handler)
                link_error_handler(*this, remote_addr, connection_state::connection_refuse);
            return;
        }

        if (head.version != 4)
        {
            if (link_error_handler)
                link_error_handler(*this, remote_addr, connection_state::invalid_request);
            return;
        }
        if (head.v4.msg_type == tracker_packet::pong) // pong
        {
            auto len = head.v4.size;
            if (len != sizeof(tracker_ping_pong_t)) // TL.DR
                return;
            tracker_ping_pong_t pong;

            socket_buffer_t buffer = socket_buffer_t::from_struct(pong);
            buffer.expect().origin_length();
            if (co::await(tcp::conn_aread_packet_content, conn, buffer) != io_result::ok)
                return;

            endian::cast_inplace(pong, buffer);
            endian::cast(pong.port);
            endian::cast(pong.udp_port);
            // save
            update_tracker(remote_addr, conn, pong);
        }
        else
        {
            if (link_error_handler)
                link_error_handler(*this, remote_addr, connection_state::invalid_request);
        }
    }
}

void tracker_server_t::udp_main(rudp_connection_t conn)
{
    socket_buffer_t buffer(udp.get_mtu());
    udp_connection_request_t request;
    buffer.expect().origin_length();
    int ch;
    co::await(rudp_aread, &udp, conn, buffer);
    if (buffer.get_length() != sizeof(udp_connection_request_t))
        return;

    endian::cast_to(buffer, request);
    if (request.magic != conn_request_magic)
        return;
    if (request.from_port == 0)
        return;

    socket_addr_t from_node_addr(request.target_ip, request.target_port);
    auto it = nodes.find(from_node_addr);
    if (it == nodes.end())
        return;

    auto idx = it->second;
    auto node_info = node_infos[idx];
    auto tcpconn = node_info.conn;

    /// NOTE: this port may be a NAT port
    request.from_udp_port = conn.address.get_port();
    if (node_info.conn.get_socket()->is_connection_alive())
        node_info.conn.get_socket()->start_with([tcpconn, request, this, node_info]() {
            if (normal_peer_connect_handler)
            {
                peer_node_t node;
                node.ip = request.target_ip;
                node.port = request.target_port;
                node.udp_port = 0;
                normal_peer_connect_handler(*this, node);
            }
            tcp::package_head_t head;
            head.version = 4;
            head.v4.msg_type = tracker_packet::peer_request;
            socket_buffer_t buffer = socket_buffer_t::from_struct(request);
            buffer.expect().origin_length();
            endian::cast_inplace(request, buffer);
            co::await(tcp::conn_awrite_packet, tcpconn, head, buffer);
        });
}

tracker_server_t::~tracker_server_t() { close(); }

void tracker_server_t::close()
{
    server.close_server();
    if (udp.is_bind())
        udp.close();
}

void tracker_server_t::config(std::string edge_key) { this->edge_key = edge_key; }

void tracker_server_t::bind(event_context_t &context, socket_addr_t addr, int max_client_count, bool reuse_addr)
{
    server.on_client_join(std::bind(&tracker_server_t::server_main, this, std::placeholders::_2));
    server
        .on_client_error([this](tcp::server_t &, socket_t *so, socket_addr_t remote, connection_state state) {
            auto it = trackers.find(remote);
            if (it != trackers.end())
            {
                it->second->closed = true;
                if (link_error_handler)
                    link_error_handler(*this, remote, state);
            }
            else
            {
                auto it = nodes.find(remote);
                if (it != nodes.end())
                {
                    auto idx = it->second;
                    auto &node = node_infos[idx];
                    if (peer_error_handler)
                        peer_error_handler(*this, remote, node.sid, state);
                }
            }
        })
        .on_client_exit([this](tcp::server_t &, tcp::connection_t conn) {
            auto remote = conn.get_socket()->remote_addr();
            auto it = trackers.find(remote);
            if (it != trackers.end())
            {
                it->second->closed = true;
                if (unlink_handler)
                    unlink_handler(*this, remote);
            }
            else
            {
                auto it = nodes.find(remote);
                if (it != nodes.end())
                {
                    auto idx = it->second;
                    auto &node = node_infos[idx];
                    if (remove_handler)
                        remove_handler(*this, node.node, node.sid);
                }
            }
        });

    server.listen(context, addr, max_client_count, reuse_addr);

    udp.on_unknown_packet([this](socket_addr_t addr) {
        udp.add_connection(addr, 0, make_timespan(10));
        return true;
    });

    udp.bind(context);
    udp_port = udp.get_socket()->local_addr().get_port();
    udp.on_new_connection(std::bind(&tracker_server_t::udp_main, this, std::placeholders::_1));
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
        p.client
            .on_server_error([this, addr](tcp::client_t &, socket_t *so, socket_addr_t remote, connection_state state) {
                auto it = trackers.find(addr);
                if (it != trackers.end())
                    it->second->closed = true;
                if (link_error_handler)
                    link_error_handler(*this, addr, state);
            })
            .on_server_disconnect([addr, this](tcp::client_t &c, tcp::connection_t conn) {
                if (unlink_handler)
                    unlink_handler(*this, addr);
            });

        p.client.connect(context, addr, timeout);
    }
}

tracker_server_t &tracker_server_t::on_link_error(link_error_handler_t handler)
{
    link_error_handler = handler;
    return *this;
}

tracker_server_t &tracker_server_t::on_link_server(link_handler_t handler)
{
    link_handler = handler;
    return *this;
}

tracker_server_t &tracker_server_t::on_unlink_server(link_handler_t handler)
{
    unlink_handler = handler;
    return *this;
}

tracker_server_t &tracker_server_t::on_shared_peer_add_connection(peer_add_handler_t handler)
{
    add_handler = handler;
    return *this;
}

tracker_server_t &tracker_server_t::on_shared_peer_remove_connection(peer_remove_handler_t handler)
{
    remove_handler = handler;
    return *this;
}

tracker_server_t &tracker_server_t::on_shared_peer_error(peer_error_handler_t handler)
{
    peer_error_handler = handler;
    return *this;
}

tracker_server_t &tracker_server_t::on_normal_peer_connect(peer_connect_handler_t handler)
{
    normal_peer_connect_handler = handler;
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

/// ------------------------------------client

void heartbeat(tcp::connection_t conn)
{
    tcp::package_head_t head;
    head.version = 4;
    head.v4.msg_type = tracker_packet::heartbeat;
    socket_buffer_t buffer(nullptr, 0);
    buffer.expect().length(0);
    co::await(tcp::conn_awrite_packet, conn, head, buffer);
}

void update_info(tcp::connection_t conn)
{
    tcp::package_head_t head;
    head.version = 4;
    head.v4.msg_type = tracker_packet::get_tracker_info_request;
    socket_buffer_t buffer(nullptr, 0);
    buffer.expect().length(0);
    co::await(tcp::conn_awrite_packet, conn, head, buffer);
}

void init_long_connection(tcp::connection_t conn, u64 sid, std::string key)
{
    tcp::package_head_t head;
    head.version = 4;
    head.v4.msg_type = tracker_packet::init_connection;
    init_connection_t init;
    socket_buffer_t buffer = socket_buffer_t::from_struct(init);
    buffer.expect().origin_length();
    init.register_sid = sid;
    for (int i = 0; i < key.size() && i < sizeof(init.key); i++)
    {
        init.key[i] = key[i];
    }
    endian::cast_inplace(init, buffer);
    if (co::await(tcp::conn_awrite_packet, conn, head, buffer) != io_result::ok)
        return;
}

void tracker_node_client_t::main(tcp::connection_t conn)
{
    if (!is_peer_client)
    {
        init_long_connection(conn, sid, key);
    }
    /// first get network address info
    update_info(conn);

    if (tracker_connect_handler)
        tracker_connect_handler(*this, conn.get_socket()->remote_addr());

    while (1)
    {
        if (request_trackers)
        {
            update_trackers(50);
            request_trackers = false;
        }
        update_nodes();

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

        if (head.v4.msg_type == tracker_packet::get_tracker_info_respond)
        {
            get_tracker_info_respond_t respond;

            if (head.v4.size != sizeof(respond))
                return;
            socket_buffer_t buffer((byte *)&respond, sizeof(respond));
            buffer.expect().origin_length();
            co::await(tcp::conn_aread_packet_content, conn, buffer);
            endian::cast_inplace(respond, buffer);
            server_udp_address = socket_addr_t(conn.get_socket()->remote_addr().v4_addr(), respond.tudp_port);
            client_outer_port = respond.cport;
            client_outer_ip = respond.cip;
            client_rudp_port = respond.tudp_port;
        }
        else if (head.v4.msg_type == tracker_packet::get_nodes_respond)
        {
            if (head.v4.size < sizeof(get_nodes_respond_t))
                return;
            get_nodes_respond_t respond;
            // buffer in stack
            socket_buffer_t recv_buffer = socket_buffer_t::from_struct(respond);
            recv_buffer.expect().origin_length();
            co::await(tcp::conn_aread_packet_content, conn, recv_buffer);
            endian::cast_inplace(respond, recv_buffer);
            if (respond.return_count > 1000)
                return;
            /// get rest node
            if (respond.return_count > 0)
            {
                std::unique_ptr<char[]> data =
                    std::make_unique<char[]>(respond.return_count * sizeof(tracker_peer_node_t));
                socket_buffer_t recv_data_buffer((byte *)data.get(),
                                                 respond.return_count * sizeof(tracker_peer_node_t));
                recv_data_buffer.expect().origin_length();
                co::await(tcp::conn_aread_packet_content, conn, recv_data_buffer);
                tracker_peer_node_t *node = (tracker_peer_node_t *)data.get();
                bool include_self = false;
                for (auto i = 0; i < respond.return_count; i++)
                {
                    endian::cast_inplace(node[i], recv_data_buffer);
                    recv_data_buffer.walk_step(sizeof(tracker_peer_node_t));
                }
                if (node_update_handler)
                    node_update_handler(*this, (peer_node_t *)node, (u64)respond.return_count);
            }
            else if (node_update_handler)
                node_update_handler(*this, (peer_node_t *)nullptr, 0);
        }
        else if (head.v4.msg_type == tracker_packet::get_trackers_respond)
        {
            if (head.v4.size < sizeof(get_trackers_respond_t))
                return;
            get_trackers_respond_t respond;
            // buffer in stack
            socket_buffer_t recv_buffer = socket_buffer_t::from_struct(respond);
            recv_buffer.expect().origin_length();
            co::await(tcp::conn_aread_packet_content, conn, recv_buffer);
            endian::cast_inplace(respond, recv_buffer);
            if (respond.return_count > 1000)
                return;
            /// get rest node
            std::unique_ptr<char[]> data =
                std::make_unique<char[]>(respond.return_count * sizeof(tracker_tracker_node_t));
            socket_buffer_t recv_data_buffer((byte *)data.get(), respond.return_count * sizeof(tracker_tracker_node_t));
            recv_data_buffer.expect().origin_length();
            co::await(tcp::conn_aread_packet_content, conn, recv_data_buffer);
            tracker_tracker_node_t *node = (tracker_tracker_node_t *)data.get();
            for (auto i = 0; i < respond.return_count; i++)
            {
                endian::cast_inplace(node[i], recv_data_buffer);
                recv_data_buffer.walk_step(sizeof(tracker_tracker_node_t));
            }
            if (tracker_update_handler)
                tracker_update_handler(*this, (tracker_node_t *)node, (u64)respond.return_count);
        }
        else if (head.v4.msg_type == tracker_packet::peer_request)
        {
            if (head.v4.size != sizeof(udp_connection_request_t))
                return;
            udp_connection_request_t request;
            socket_buffer_t buffer = socket_buffer_t::from_struct(request);
            buffer.expect().origin_length();

            co::await(tcp::conn_aread_packet_content, conn, buffer);
            endian::cast_inplace(request, buffer);

            if (request.magic != conn_request_magic || (request.sid != sid && sid != 0))
                continue;
            peer_node_t node;
            node.ip = request.from_ip;
            node.port = request.from_port;
            node.udp_port = request.from_udp_port;

            if (connect_handler)
                connect_handler(*this, node);
        }
        else
        {
            return;
        }
    }
}

void tracker_node_client_t::config(bool as_peer_server, u64 sid, std::string key)
{
    assert(!client.is_connect());
    this->key = key;
    this->sid = sid;
    this->is_peer_client = !as_peer_server;
}

void tracker_node_client_t::connect_server(event_context_t &context, socket_addr_t addr, microsecond_t timeout)
{
    remote_server_address = addr;
    this->context = &context;
    client_rudp_port = 0;

    wait_next_package = false;
    client.on_server_connect(std::bind(&tracker_node_client_t::main, this, std::placeholders::_2))
        .on_server_error([this](tcp::client_t &c, socket_t *so, socket_addr_t addr, connection_state state) {
            if (error_handler)
                error_handler(*this, addr, state);
        });
    server_udp_address = {};
    client.connect(context, addr, timeout);
}

void tracker_node_client_t::update_nodes()
{
    while (!node_queue.empty())
    {
        auto node = node_queue.front();
        node_queue.pop();

        tcp::package_head_t head;
        head.version = 4;
        head.v4.msg_type = tracker_packet::get_nodes_request;
        get_nodes_request_t request;
        request.sid = sid;
        request.strategy = std::get<request_strategy>(node);
        request.max_count = std::get<int>(node);
        socket_buffer_t buffer = socket_buffer_t::from_struct(request);
        buffer.expect().origin_length();
        endian::cast_inplace(request, buffer);
        tcp::connection_t conn = client.get_connection();
        co::await(tcp::conn_awrite_packet, conn, head, buffer);
    }
}

void tracker_node_client_t::update_trackers(int count)
{
    tcp::package_head_t head;
    head.version = 4;
    head.v4.msg_type = tracker_packet::get_trackers_request;
    get_trackers_request_t request;
    request.max_count = count;
    socket_buffer_t buffer = socket_buffer_t::from_struct(request);
    buffer.expect().origin_length();
    endian::cast_inplace(request, buffer);
    tcp::connection_t conn = client.get_connection();
    co::await(tcp::conn_awrite_packet, conn, head, buffer);
}

void tracker_node_client_t::request_update_trackers()
{
    if (client.is_connect())
    {
        if (wait_next_package)
        {
            client.get_socket()->start_with([this]() { update_trackers(100); });
        }
        return;
    }
    request_trackers = true;
    client.connect(*context, remote_server_address, timeout);
}

void tracker_node_client_t::request_update_nodes(int max_count, request_strategy strategy)
{
    node_queue.push(std::make_tuple(max_count, strategy));

    if (client.is_connect())
    {
        if (wait_next_package)
            client.get_socket()->start_with([this]() { update_nodes(); });
        return;
    }
    client.connect(*context, remote_server_address, timeout);
}

void tracker_node_client_t::request_connect_node(peer_node_t node, rudp_t &udp)
{
    udp.add_connection(server_udp_address, 0, timeout, [this, &udp, node](rudp_connection_t conn) {
        udp_connection_request_t req;
        socket_buffer_t buffer = socket_buffer_t::from_struct(req);
        req.magic = conn_request_magic;
        req.from_ip = client_outer_ip;
        req.from_port = client_outer_port;
        req.from_udp_port = udp.get_socket()->local_addr().get_port();
        req.target_ip = node.ip;
        req.target_port = node.port;
        req.sid = sid;
        buffer.expect().origin_length();
        endian::cast_inplace(req, buffer);
        co::await(rudp_awrite, &udp, conn, buffer);
    });
}

tracker_node_client_t &tracker_node_client_t::on_node_request_connect(nodes_connect_handler_t handler)
{
    connect_handler = handler;
    return *this;
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

tracker_node_client_t &tracker_node_client_t::on_tracker_server_connect(tracker_connect_handler_t handler)
{
    tracker_connect_handler = handler;
    return *this;
}

void tracker_node_client_t::close() { client.close(); }

tracker_node_client_t::~tracker_node_client_t() { close(); }

} // namespace net::p2p
