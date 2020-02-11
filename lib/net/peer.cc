#include "net/peer.hpp"
#include "net/co.hpp"
#include "net/endian.hpp"
#include "net/load_balance.hpp"
#include "net/socket.hpp"
#include "net/socket_buffer.hpp"
namespace net::peer
{
/// client -------------------------------------------------------------------------------------
peer_client_t::peer_client_t(session_id_t sid)
    : sid(sid)
{
}

peer_client_t::~peer_client_t() {}

peer_t *peer_client_t::add_peer(event_context_t &context, socket_addr_t peer_addr)
{
    auto peer = std::make_unique<peer_t>();
    peer->remote_address = peer_addr;
    socket_addr_t addr(0);
    peer->udp.bind(context, addr);
    auto ptr = peer.get();
    peers.push_back(std::move(peer));
    return ptr;
}

void peer_client_t::client_main(peer_t *peer)
{
    auto socket = peer->udp.get_socket();
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
        if (co::await(socket_awrite_to, socket, buffer, peer->remote_address) != io_result::ok)
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

    socket_buffer_t recv_buffer(1472);
    peer_data_package_t data;
    while (1)
    {
        socket_addr_t addr;
        recv_buffer.expect().origin_length();
        co::await(socket_aread_from, socket, recv_buffer, addr);
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
            endian::cast_to(recv_buffer, data);
            if (handler)
                handler(*this, data, peer);
        }
        else if (type == peer_msg_type::heart) // heart
        {
            peer->last_online_timestamp = get_timestamp();
        }
    }
}

void peer_client_t::connect_to_peer(peer_t *peer) { peer->udp.run(std::bind(&peer_client_t::client_main, this, peer)); }

peer_client_t &peer_client_t::at_peer_disconnect(peer_disconnect_t handler)
{
    this->disconnect_handler = handler;
    return *this;
}

peer_client_t &peer_client_t::at_peer_connect(peer_connect_ok_t handler)
{
    this->connect_handler = handler;
    return *this;
}

peer_client_t &peer_client_t::at_peer_data_recv(peer_server_data_recv_t handler)
{
    this->handler = handler;
    return *this;
}

void peer_client_t::request_data_from_peer(u64 data_id)
{
    auto idx = rand() % peers.size();
    auto peer = peers[idx].get();
    peer->current_request_data_id = data_id;
    peer->in_request = true;
}

/// server-------------------------------------------------------------------------------------------

void peer_server_t::server_main()
{
    socket_addr_t addr;
    socket_buffer_t buffer(1472);
    while (1)
    {
        buffer.expect().origin_length();
        co::await(socket_aread_from, server.get_socket(), buffer, addr);
        u8 type = buffer.get_step_ptr()[0];
        if (type == peer_msg_type::init_request) // init request
        {
            auto it = peers_map.find(addr);
            if (it == peers_map.end())
            {
                auto peer = std::make_unique<speer_t>();
                peer->last_online_timestamp = get_timestamp();
                peer->ping_ok = true;
                auto ptr = peer.get();
                peers_map.emplace(addr, std::move(peer));
                peer_init_respond_t respond;
                respond.type = 1;
                respond.first_data_id = 0;
                respond.last_data_id = 0;
                buffer.expect().length(sizeof(respond));
                assert(endian::save_to(respond, buffer));
                co::await(socket_awrite_to, server.get_socket(), buffer, addr);
                if (client_handler)
                    client_handler(*this, ptr);
            }
            // else aready done
        }
        else if (type == peer_msg_type::data_request)
        {
            // request data
            auto it = peers_map.find(addr);
            if (it == peers_map.end())
            {
                continue;
            }
            peer_data_request_t request;
            assert(endian::cast_to(buffer, request));
            if (data_handler)
                data_handler(*this, request, it->second.get());
        }
        else if (type == peer_msg_type::heart)
        {
            auto it = peers_map.find(addr);
            if (it == peers_map.end())
            {
                continue;
            }
            it->second->last_online_timestamp = get_timestamp();
        }
    }
}

void peer_server_t::bind_server(event_context_t &context, socket_addr_t bind_taddr, bool reuse_addr)
{
    server.bind(context, bind_taddr);
    server.run(std::bind(&peer_server_t::server_main, this));
}

peer_server_t &peer_server_t::at_client_join(client_join_handler_t handler)
{
    client_handler = handler;
    return *this;
}

peer_server_t &peer_server_t::at_request_data(client_data_request_handler_t handler)
{
    data_handler = handler;
    return *this;
}

} // namespace net::peer