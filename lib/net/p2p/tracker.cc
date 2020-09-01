#include "net/p2p/tracker.hpp"
#include "net/socket.hpp"
#include <algorithm>
#include <random>
#include <vector>

namespace net::p2p
{

const u32 magic = 0x5faf2412;

io_result do_ping_pong(tcp::connection_t conn, u16 port, u16 udp_port, u32 workload, u32 trackers, int code)
{
    tcp::package_head_t head;
    Package pkg;
    if (code == 0)
    {
        auto pong = pkg.mutable_ping();
        pong->set_port(port);
        pong->set_udp_port(udp_port);
        pong->set_peer_workload(workload);
        pong->set_tracker_neighbor_count(trackers);
    }
    else
    {

        auto pong = pkg.mutable_pong();
        pong->set_port(port);
        pong->set_udp_port(udp_port);
        pong->set_peer_workload(workload);
        pong->set_tracker_neighbor_count(trackers);
    }
    socket_buffer_t buffer(pkg);
    return co::await(tcp::conn_awrite_packet, conn, head, buffer);
}

void tracker_server_t::update_tracker(socket_addr_t addr, tcp::connection_t conn, PingPong &res)
{
    auto it = trackers.find(addr);
    if (it == trackers.end())
    {
        it = trackers.insert(std::make_pair(addr, std::make_unique<tracker_info_t>())).first;
        auto &pnode = *it->second.get();
        pnode.is_client = false;
        pnode.node.ip = addr.v4_addr();
        pnode.node.port = res.port();
        pnode.node.udp_port = res.udp_port();
    }
    auto &pnode = *it->second.get();

    pnode.workload = res.peer_workload();
    pnode.trackers = res.tracker_neighbor_count();
    pnode.conn_server = conn;
    pnode.last_ping = get_current_time();
}

void tracker_server_t::server_main(tcp::connection_t conn)
{
    tcp::package_head_t head;
    auto remote_addr = conn.get_socket()->remote_addr();
    Package pkg;
    while (1)
    {
        if (co::await(tcp::conn_aread_packet_head, conn, head) != io_result::ok)
            return;

        auto len = head.size;
        socket_buffer_t buffer(len);
        buffer.expect().origin_length();
        if (co::await(tcp::conn_aread_packet_content, conn, buffer) != io_result::ok)
            return;
        pkg.ParseFromArray(buffer.get(), buffer.get_length());
        if (pkg.has_ping())
        {
            PingPong &ping = *pkg.mutable_ping();

            // save
            update_tracker(remote_addr, conn, ping);
            int local_tcp_port = server.get_socket()->local_addr().get_port();
            int local_udp_port = udp.get_socket()->local_addr().get_port();

            do_ping_pong(conn, local_tcp_port, local_udp_port, nodes.size(), trackers.size(), 1);
        }
        else if (pkg.has_tracker_info_req())
        {
            Package pkg;
            TrackerInfoRsp &rsp = *pkg.mutable_tracker_info_rsp();
            /// get address NAT
            rsp.set_port(conn.get_socket()->remote_addr().get_port());
            rsp.set_ip(conn.get_socket()->remote_addr().v4_addr());
            rsp.set_udp_port(udp_port);
            socket_buffer_t buffer(pkg);

            co::await(tcp::conn_awrite_packet, conn, head, buffer);
        }
        else if (pkg.has_node_req())
        {
            NodeReq &req = *pkg.mutable_node_req();
            Package pkg;
            NodeRsp &rsp = *pkg.mutable_node_rsp();

            int cnt = std::min((int)req.max_count(), 500);
            // use buffer from 'data'
            auto cur_time = get_current_time();
            if (req.strategy() == RequestNodeStrategy::random)
            {
                rsp.set_avl_count(node_infos.size());
                if (node_infos.size() > 0)
                {
                    int start = rand() % node_infos.size();
                    bool over = false;
                    for (int j = start; rsp.nodes_size() < cnt; j++)
                    {
                        if (j >= node_infos.size())
                        {
                            over = true;
                            j = 0;
                        }
                        if (j == start && over)
                            break;

                        if (req.sid() == node_infos[j].sid &&
                            cur_time - node_infos[j].last_ping <= node_tick_times * node_tick_timespan)
                        {
                            // add
                            auto &node = *rsp.add_nodes();
                            node.set_ip(node_infos[j].node.ip);
                            node.set_port(node_infos[j].node.port);
                            node.set_udp_port(node_infos[j].node.udp_port);
                        }
                    }
                }
            }
            else if (req.strategy() == RequestNodeStrategy::min_workload)
            {
                /// TODO: sort by workload and return to client
            }
            else if (req.strategy() == RequestNodeStrategy::edge_nodes)
            {
                if (node_infos.size() > 0)
                {
                    int start = rand() % node_infos.size();
                    bool over = false;
                    int m = 0;
                    for (auto &x : node_infos)
                    {
                        if (x.sid == 0)
                        {
                            m++;
                        }
                    }
                    rsp.set_avl_count(m);
                    for (int j = start; rsp.nodes_size() < cnt; j++)
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
                            auto &node = *rsp.add_nodes();
                            node.set_ip(node_infos[j].node.ip);
                            node.set_port(node_infos[j].node.port);
                            node.set_udp_port(node_infos[j].node.udp_port);
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
            rsp.set_sid(req.sid());
            {

                socket_buffer_t buffer(pkg);
                co::await(tcp::conn_awrite_packet, conn, head, buffer);
            }
        }
        else if (pkg.has_trackers_req())
        {
            TrackersReq &req = *pkg.mutable_trackers_req();
            Package pkg;
            TrackersRsp &rsp = *pkg.mutable_trackers_rsp();

            auto cnt = std::min(500u, req.max_count());
            // set respond header
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
                if (i < cnt)
                {
                    auto &t = *rsp.add_trackers();
                    t.set_port(node.port);
                    t.set_udp_port(node.udp_port);
                    t.set_ip(node.ip);
                }
                i++;
                ++it;
            }
            // set head
            rsp.set_avl_count(i);
            {

                socket_buffer_t buffer(pkg);
                // send
                co::await(tcp::conn_awrite_packet, conn, head, buffer);
            }
        }
        else if (pkg.has_init_connection_req())
        {
            InitConnectionReq &req = *pkg.mutable_init_connection_req();

            if (req.sid() == 0) // if sid == 0. it is edge server
            {
                // check edge key
                if (req.key() != edge_key)
                {
                    if (peer_error_handler)
                    {
                        peer_error_handler(*this, remote_addr, req.sid(), connection_state::secure_check_failed);
                    }
                    return;
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
            node.sid = req.sid();
            if (add_handler)
                add_handler(*this, node.node, node.sid);
        }
        else if (pkg.has_heart())
        {
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

    int local_tcp_port = server.get_socket()->local_addr().get_port();
    int local_udp_port = udp.get_socket()->local_addr().get_port();
    do_ping_pong(conn, local_tcp_port, local_udp_port, nodes.size(), trackers.size(), 0);
    Package pkg;

    while (1)
    {
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
                else
                {
                    int local_tcp_port = server.get_socket()->local_addr().get_port();
                    int local_udp_port = udp.get_socket()->local_addr().get_port();
                    do_ping_pong(conn, local_tcp_port, local_udp_port, nodes.size(), trackers.size(), 0);
                }
            }
            else
            {
                int local_tcp_port = server.get_socket()->local_addr().get_port();
                int local_udp_port = udp.get_socket()->local_addr().get_port();
                do_ping_pong(conn, local_tcp_port, local_udp_port, nodes.size(), trackers.size(), 0);
            }
        }
        else if (ret != io_result::ok)
        {
            if (link_error_handler)
                link_error_handler(*this, remote_addr, connection_state::connection_refuse);
            return;
        }

        auto len = head.size;
        socket_buffer_t buffer(len);
        buffer.expect().origin_length();
        if (co::await(tcp::conn_aread_packet_content, conn, buffer) != io_result::ok)
            return;
        pkg.ParseFromArray(buffer.get(), buffer.get_length());
        if (pkg.has_pong()) // pong
        {
            PingPong &pong = *pkg.mutable_pong();

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
    buffer.expect().origin_length();
    co::await(rudp_aread, &udp, conn, buffer);
    Package pkg;
    UDPConnectionReq &req = *pkg.mutable_udp_connection_req();
    req.ParseFromArray(buffer.get(), buffer.get_length());

    if (req.magic() != magic)
        return;
    if (req.from_port() == 0)
        return;

    socket_addr_t from_node_addr(req.target_ip(), req.target_port());
    auto it = nodes.find(from_node_addr);
    if (it == nodes.end())
        return;

    auto idx = it->second;
    auto node_info = node_infos[idx];
    auto tcpconn = node_info.conn;

    /// NOTE: this port may be a NAT port
    req.set_from_udp_port(conn.address.get_port());
    if (node_info.conn.get_socket()->is_connection_alive())
        node_info.conn.get_socket()->start_with([tcpconn, pkg, this, node_info]() {
            if (normal_peer_connect_handler)
            {
                peer_node_t node;
                node.ip = pkg.udp_connection_req().target_ip();
                node.port = pkg.udp_connection_req().target_port();
                node.udp_port = 0;
                normal_peer_connect_handler(*this, node);
            }
            tcp::package_head_t head;
            socket_buffer_t buffer(pkg);
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
    Package pkg;
    Heart &req = *pkg.mutable_heart();

    socket_buffer_t buffer(pkg);
    co::await(tcp::conn_awrite_packet, conn, head, buffer);
}

void update_info(tcp::connection_t conn)
{
    tcp::package_head_t head;
    Package pkg;
    TrackerInfoReq req = *pkg.mutable_tracker_info_req();

    socket_buffer_t buffer(pkg);
    co::await(tcp::conn_awrite_packet, conn, head, buffer);
}

void init_long_connection(tcp::connection_t conn, u64 sid, std::string key)
{
    tcp::package_head_t head;
    Package pkg;
    InitConnectionReq &req = *pkg.mutable_init_connection_req();
    req.set_sid(sid);
    req.set_key(key);

    socket_buffer_t buffer(pkg);
    if (co::await(tcp::conn_awrite_packet, conn, head, buffer) != io_result::ok)
        return;
}

void tracker_node_client_t::tmain(tcp::connection_t conn)
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
        auto len = head.size;
        socket_buffer_t buffer(len);
        buffer.expect().origin_length();
        co::await(tcp::conn_aread_packet_content, conn, buffer);
        Package pkg;
        pkg.ParseFromArray(buffer.get(), buffer.get_length());
        if (pkg.has_tracker_info_rsp())
        {
            TrackerInfoRsp &rsp = *pkg.mutable_tracker_info_rsp();
            server_udp_address = socket_addr_t(conn.get_socket()->remote_addr().v4_addr(), rsp.udp_port());
            client_outer_port = rsp.port();
            client_outer_ip = rsp.ip();
            client_rudp_port = rsp.udp_port();
        }
        else if (pkg.has_node_rsp())
        {
            NodeRsp &rsp = *pkg.mutable_node_rsp();
            /// get rest node
            if (rsp.nodes_size() > 0)
            {
                std::vector<peer_node_t> nodes;
                for (auto i = 0; i < rsp.nodes_size(); i++)
                {
                    peer_node_t peer;
                    auto &m = rsp.nodes(i);
                    peer.ip = m.ip();
                    peer.port = m.port();
                    peer.udp_port = m.udp_port();
                    nodes.push_back(peer);
                }
                if (node_update_handler)
                    node_update_handler(*this, (peer_node_t *)nodes.data(), (u64)nodes.size());
            }
            else if (node_update_handler)
                node_update_handler(*this, (peer_node_t *)nullptr, 0);
        }
        else if (pkg.has_trackers_rsp())
        {
            TrackersRsp &rsp = *pkg.mutable_trackers_rsp();
            /// get rest node
            std::vector<tracker_node_t> nodes;
            for (auto i = 0; i < rsp.trackers_size(); i++)
            {
                auto &t = rsp.trackers(i);
                tracker_node_t tracker;
                tracker.ip = t.ip();
                tracker.port = t.port();
                tracker.udp_port = t.udp_port();
                nodes.push_back(tracker);
            }
            if (tracker_update_handler)
                tracker_update_handler(*this, (tracker_node_t *)nodes.data(), (u64)nodes.size());
        }
        else if (pkg.has_udp_connection_req())
        {
            UDPConnectionReq &req = *pkg.mutable_udp_connection_req();

            if (req.magic() != magic || (req.sid() != sid && sid != 0))
                continue;
            peer_node_t node;
            node.ip = req.from_ip();
            node.port = req.from_port();
            node.udp_port = req.from_udp_port();

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
    client.on_server_connect(std::bind(&tracker_node_client_t::tmain, this, std::placeholders::_2))
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
        Package pkg;
        NodeReq &req = *pkg.mutable_node_req();
        req.set_sid(sid);
        req.set_strategy(std::get<RequestNodeStrategy>(node));
        req.set_max_count(std::get<int>(node));

        socket_buffer_t buffer(pkg);
        tcp::connection_t conn = client.get_connection();
        co::await(tcp::conn_awrite_packet, conn, head, buffer);
    }
}

void tracker_node_client_t::update_trackers(int count)
{
    tcp::package_head_t head;
    Package pkg;
    TrackersReq &req = *pkg.mutable_trackers_req();
    req.set_max_count(count);
    socket_buffer_t buffer(pkg);
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

void tracker_node_client_t::request_update_nodes(int max_count, RequestNodeStrategy strategy)
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
        Package pkg;

        UDPConnectionReq &req = *pkg.mutable_udp_connection_req();
        req.set_magic(magic);
        req.set_from_ip(client_outer_ip);
        req.set_from_port(client_outer_port);
        req.set_from_udp_port(udp.get_socket()->local_addr().get_port());
        req.set_target_ip(node.ip);
        req.set_target_port(node.port);
        req.set_sid(sid);
        socket_buffer_t buffer(pkg);
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
