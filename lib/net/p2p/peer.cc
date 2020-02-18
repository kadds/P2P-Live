#include "net/p2p/peer.hpp"
#include "net/co.hpp"
#include "net/endian.hpp"
#include "net/load_balance.hpp"
#include "net/socket.hpp"
#include "net/socket_buffer.hpp"
namespace net::p2p
{
/// client -------------------------------------------------------------------------------------
peer_client_t::peer_client_t(session_id_t sid)
    : sid(sid)
{
}

peer_client_t::~peer_client_t() {}

peer_t *peer_client_t::add_peer(event_context_t &context)
{
    auto peer = std::make_unique<peer_t>();
    socket_addr_t addr(0);
    peer->udp.bind(context, addr);
    auto ptr = peer.get();
    peers.push_back(std::move(peer));
    return ptr;
}

void peer_client_t::client_main(peer_t *peer)
{
    auto addr = peer->udp.get_socket()->local_addr();
    {
        // request connect
        // swap init data
        peer_init_request_t req;
        req.type = peer_msg_type::init_request;
        req.sid = sid;
        socket_buffer_t buffer(sizeof(req));
        buffer.expect().origin_length();
        assert(endian::save_to(req, buffer));
        if (co::await(rudp_awrite, &peer->udp, buffer) != io_result::ok)
        {
            for (auto it = peers.begin(); it != peers.end(); it++)
            {
                if (it->get() == peer)
                {
                    peers.erase(it);
                }
            }
            if (disconnect_handler)
                disconnect_handler(*this, peer);
            return;
        }
    }

    std::unique_ptr<char[]> data = std::make_unique<char[]>(1472);
    socket_buffer_t recv_buffer((byte *)data.get(), 1472);

    peer_data_package_t *package = (peer_data_package_t *)data.get();
    while (1)
    {
        recv_buffer.expect().origin_length();
        co::await(rudp_aread, &peer->udp, recv_buffer);
        u8 type = recv_buffer.get_step_ptr()[0];
        if (type == peer_msg_type::init_respond)
        {
            peer_init_respond_t respond;
            assert(endian::cast_to(recv_buffer, respond));
            peer->last_online_timestamp = get_timestamp();
            peer->ping_ok = true;
            if (connect_handler)
                connect_handler(*this, peer);
        }
        else if (type == peer_msg_type::data_package)
        {
            endian::cast_to(recv_buffer, *package);
            memcpy(&package->data, recv_buffer.get_raw_ptr() + sizeof(peer_data_package_t),
                   recv_buffer.get_data_length() - sizeof(peer_data_package_t));
            if (handler)
                handler(*this, *package, peer);
        }
        else if (type == peer_msg_type::heart) // heart
        {
            peer->last_online_timestamp = get_timestamp();
        }
    }
}

void peer_client_t::connect_to_peer(peer_t *peer, socket_addr_t server_addr)
{
    peer->remote_address = server_addr;
    peer->udp.connect(server_addr);
    peer->udp.run(std::bind(&peer_client_t::client_main, this, peer));
}

peer_client_t &peer_client_t::on_peer_disconnect(peer_disconnect_t handler)
{
    this->disconnect_handler = handler;
    return *this;
}

peer_client_t &peer_client_t::on_peer_connect(peer_connect_ok_t handler)
{
    this->connect_handler = handler;
    return *this;
}

peer_client_t &peer_client_t::on_peer_data_recv(peer_server_data_recv_t handler)
{
    this->handler = handler;
    return *this;
}

void peer_client_t::pull_data_from_peer(u64 data_id)
{
    /// here make a choice to select a peer to pull data

    auto idx = rand() % peers.size();
    auto peer = peers[idx].get();
    peer->current_request_data_id = data_id;
    peer->in_request = true;
    peer_data_request_t request;
    request.data_id = data_id;
    request.priority = 0;
    request.type = peer_msg_type::data_request;
    socket_buffer_t buffer(sizeof(request));
    buffer.expect().origin_length();
    assert(endian::save_to(request, buffer));
    // pull data;
    co::await(rudp_awrite, &peer->udp, buffer);
}

/// server-------------------------------------------------------------------------------------------

void peer_server_t::peer_main(speer_t *peer)
{
    socket_buffer_t buffer(1472);
    peer->udp.connect(peer->remote_address);
    while (1)
    {
        buffer.expect().origin_length();
        co::await(rudp_aread, &peer->udp, buffer);
        u8 type = buffer.get_step_ptr()[0];
        if (type == peer_msg_type::init_request) // init request
        {
            peer_init_respond_t respond;
            respond.type = 1;
            respond.first_data_id = 0;
            respond.last_data_id = 0;
            buffer.expect().length(sizeof(respond));
            assert(endian::save_to(respond, buffer));
            co::await(rudp_awrite, &peer->udp, buffer);
            if (client_handler)
                client_handler(*this, peer);
        }
        else if (type == peer_msg_type::data_request)
        {
            // request data
            peer_data_request_t request;
            assert(endian::cast_to(buffer, request));
            if (data_handler)
                data_handler(*this, request, peer);
        }
        else if (type == peer_msg_type::heart)
        {
            peer->last_online_timestamp = get_timestamp();
        }
    }
}

speer_t *peer_server_t::add_peer(event_context_t &context, socket_addr_t addr)
{
    auto peer = std::make_unique<speer_t>();
    peer->last_online_timestamp = get_timestamp();
    peer->ping_ok = true;
    peer->remote_address = addr;
    peer->udp.bind(context, socket_addr_t());
    auto ptr = peer.get();
    peers_map.emplace(addr, std::move(peer));
    ptr->udp.run(std::bind(&peer_server_t::peer_main, this, ptr));
    return ptr;
}

peer_server_t &peer_server_t::on_client_join(client_join_handler_t handler)
{
    client_handler = handler;
    return *this;
}

peer_server_t &peer_server_t::on_client_pull(client_data_request_handler_t handler)
{
    data_handler = handler;
    return *this;
}

void peer_server_t::send_package_to_peer(speer_t *peer, u64 data_id, socket_buffer_t buffer)
{
    /// split package
    peer_data_package_t package;
    package.type = peer_msg_type::data_package;
    package.data_id = data_id;
    package.package_id = 1;
    package.size = buffer.get_data_length();

    socket_buffer_t send_buffer(sizeof(package) + buffer.get_data_length());
    send_buffer.expect().origin_length();

    assert(endian::save_to(package, send_buffer));
    memcpy(send_buffer.get_raw_ptr() + sizeof(package), buffer.get_raw_ptr(), buffer.get_data_length());
    byte v = send_buffer.get_raw_ptr()[sizeof(package)];
    /// TODO: split
    co::await(rudp_awrite, &peer->udp, send_buffer);
}

} // namespace net::p2p