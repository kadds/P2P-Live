#include "net/p2p/peer.hpp"
#include "net/co.hpp"
#include "net/endian.hpp"
#include "net/load_balance.hpp"
#include "net/socket.hpp"
#include "net/socket_buffer.hpp"
namespace net::p2p
{

peer_t::peer_t(session_id_t sid)
    : sid(sid)
{
}

peer_t::~peer_t() {}

void peer_t::bind(event_context_t &context)
{
    udp.bind(context);
    udp.run(std::bind(&peer_t::main, this));
}

void peer_t::bind(event_context_t &context, socket_addr_t addr_to_bind, bool reuse_addr)
{
    udp.bind(context, addr_to_bind, reuse_addr);
    udp.run(std::bind(&peer_t::main, this));
}

peer_info_t *peer_t::add_peer()
{
    auto peer = std::make_unique<peer_info_t>();
    auto ptr = peer.get();
    noconnect_peers.emplace_back(std::move(peer));
    return ptr;
}

void peer_t::send_fragments(fragment_id_t fid, socket_buffer_t buffer, peer_info_t *target)
{
    socket_buffer_t sbuffer(1472);

    peer_fragment_respond_t *respond = (peer_fragment_respond_t *)sbuffer.get_raw_ptr();
    respond->type = peer_msg_type::fragment_respond;
    respond->fid = fid;
    respond->frame_size = buffer.get_data_length();
    int len = std::min(buffer.get_data_length(), udp.get_mtu() - sizeof(peer_fragment_respond_t));

    sbuffer.expect().length(sizeof(peer_fragment_respond_t) + len);
    endian::cast_inplace(*respond, sbuffer);
    memcpy(sbuffer.get_raw_ptr() + sizeof(peer_fragment_respond_t), buffer.get_step_ptr(), len);
    co::await(rudp_awrite, &udp, sbuffer, target->remote_address);
    if (len >= buffer.get_data_length())
        return;

    auto rest = buffer.get_data_length() - len;

    u64 start_offset = 0;
    while (start_offset >= buffer.get_data_length())
    {
        socket_buffer_t send_buffer(sizeof(respond) + buffer.get_data_length());
        send_buffer.expect().origin_length();
        endian::cast_inplace(*respond, send_buffer);
        u64 size = buffer.get_data_length() - start_offset;

        memcpy(send_buffer.get_raw_ptr() + sizeof(respond), buffer.get_raw_ptr(), buffer.get_data_length());
        co::await(rudp_awrite, &udp, send_buffer, target->remote_address);
        start_offset += size;
    }
}

void peer_t::send_init(peer_info_t *target)
{
    peer_init_request_t req;
    req.type = peer_msg_type::init_request;
    req.sid = sid;
    socket_buffer_t buffer((byte *)&req, sizeof(req));
    buffer.expect().origin_length();
    assert(endian::cast_inplace(req, buffer));
    co::await(rudp_awrite, &udp, buffer, target->remote_address);
}

void peer_t::do_write()
{
    auto peer = sendable_peers.front();
    sendable_peers.pop();

    if (!peer->has_connect)
    {
        send_init(peer);
    }
    else if (!peer->frag_request_queue.empty())
    {
        while (!peer->frag_request_queue.empty())
        {
            auto &ref = peer->frag_request_queue.front();
            update_fragments(std::move(ref.first), ref.second, peer);
            peer->frag_request_queue.pop();
        }
    }
    else if (!peer->meta_request_queue.empty())
    {
        while (!peer->meta_request_queue.empty())
        {
            auto key = peer->meta_request_queue.front();
            update_metainfo(key, peer);
            peer->meta_request_queue.pop();
        }
    }
}

peer_info_t *peer_t::find_peer(socket_addr_t addr)
{
    auto it = peers.find(addr);
    if (it != peers.end())
        return it->second.get();
    return nullptr;
}

void peer_t::main()
{
    udp.on_unknown_packet([this](socket_addr_t addr) {
        if (peers.find(addr) != peers.end())
        {
            udp.add_connection(addr, make_timespan(10));
            return true;
        }
        return false;
    });

    socket_addr_t peer_addr;
    std::unique_ptr<char[]> data = std::make_unique<char[]>(1472);
    socket_buffer_t recv_buffer((byte *)data.get(), 1472);

    while (1)
    {
        if (!sendable_peers.empty())
        {
            do_write();
        }

        recv_buffer.expect().origin_length();
        auto ret = co::await(rudp_aread, &udp, recv_buffer, peer_addr);
        if (ret == io_result::timeout)
        {
            continue;
        }
        auto peer = find_peer(peer_addr);
        if (peer == nullptr)
            continue;

        u8 type = recv_buffer.get_step_ptr()[0];
        if (type == peer_msg_type::init_request)
        {
            if (recv_buffer.get_data_length() < sizeof(peer_init_request_t))
                continue;
            peer_init_request_t *request = (peer_init_request_t *)data.get();
            endian::cast_inplace(*request, recv_buffer);
            if (request->sid != sid)
                continue;

            peer->last_ping = get_timestamp();

            if (peer->has_connect)
                continue;

            peer_init_respond_t respond;
            respond.type = peer_msg_type::init_respond;
            respond.first_data_id = 0;
            respond.last_data_id = 0;
            recv_buffer.expect().length(sizeof(respond));
            endian::save_to(respond, recv_buffer);
            co::await(rudp_awrite, &udp, recv_buffer, peer_addr);
        }
        else if (type == peer_msg_type::init_respond)
        {
            if (recv_buffer.get_data_length() < sizeof(peer_init_respond_t))
                continue;
            peer_init_respond_t *respond = (peer_init_respond_t *)data.get();
            endian::cast_inplace(*respond, recv_buffer);

            peer->last_ping = get_timestamp();
            peer->has_connect = true;

            if (connect_handler)
                connect_handler(*this, peer);
        }
        else if (type == peer_msg_type::heart)
        {
            peer->last_ping = get_timestamp();
        }
        else if (type == peer_msg_type::get_meta)
        {
            if (recv_buffer.get_data_length() < sizeof(peer_request_metainfo_t))
                continue;
            peer->last_ping = get_timestamp();
            peer_request_metainfo_t *request = (peer_request_metainfo_t *)data.get();
            if (meta_handler)
                meta_handler(*this, peer, request->key);
        }
        else if (type == peer_msg_type::meta_respond)
        {
            if (recv_buffer.get_data_length() < sizeof(peer_meta_respond_t))
                continue;
            peer_meta_respond_t *respond = (peer_meta_respond_t *)data.get();
            endian::cast_inplace(*respond, recv_buffer);
            if (meta_recv_handler)
                meta_recv_handler(*this, recv_buffer, respond->key, peer);
        }
        else if (type == peer_msg_type::fragment_request)
        {
            if (recv_buffer.get_data_length() < sizeof(peer_fragment_request_t))
                continue;
            peer_fragment_request_t *request = (peer_fragment_request_t *)data.get();
            if (request->count * sizeof(fragment_id_t) + sizeof(peer_fragment_request_t) >
                recv_buffer.get_data_length())
                continue;

            if (meta_handler)
            {
                recv_buffer.walk_step(sizeof(peer_fragment_request_t));
                for (auto i = 0; i < request->count; i++)
                {
                    endian::cast_inplace(request->ids[i], recv_buffer);
                    meta_handler(*this, peer, request->ids[i]);
                    recv_buffer.walk_step(sizeof(fragment_id_t));
                }
            }
        }
        else if (type == peer_msg_type::fragment_respond)
        {
            if (recv_buffer.get_data_length() < sizeof(peer_fragment_respond_t))
                continue;
            peer_fragment_respond_t *frag_respond = (peer_fragment_respond_t *)data.get();
            /// TODO:: packet fit it
        }
    }
}

void peer_t::connect_to_peer(peer_info_t *peer, socket_addr_t remote_addr)
{
    peer->remote_address = remote_addr;
    for (auto it = noconnect_peers.begin(); it != noconnect_peers.end(); ++it)
    {
        if (it->get() == peer)
        {

            peers.emplace(remote_addr, std::move(*it));
            noconnect_peers.erase(it);

            /// inactive timeout 10s
            udp.add_connection(remote_addr, make_timespan(10));
            sendable_peers.push(peer);
            return;
        }
    }
}

void peer_t::disconnect(peer_info_t *peer)
{
    auto it = peers.find(peer->remote_address);
    if (it != peers.end())
    {
        udp.remove_connection(peer->remote_address);
        peers.erase(it);
    }
}

peer_t &peer_t::on_peer_disconnect(peer_disconnect_t handler)
{
    this->disconnect_handler = handler;
    return *this;
}

peer_t &peer_t::on_peer_connect(peer_connect_ok_t handler)
{
    this->connect_handler = handler;
    return *this;
}

peer_t &peer_t::on_fragment_pull_request(pull_request_t handler)
{
    fragment_handler = handler;
    return *this;
}

peer_t &peer_t::on_meta_pull_request(pull_request_t handler)
{
    meta_handler = handler;
    return *this;
}

peer_t &peer_t::on_meta_data_recv(peer_data_recv_t handler)
{
    meta_recv_handler = handler;
    return *this;
}

peer_t &peer_t::on_fragment_recv(peer_data_recv_t handler)
{
    fragment_recv_handler = handler;
    return *this;
}

void peer_t::update_fragments(std::vector<fragment_id_t> ids, u8 priority, peer_info_t *target)
{
    std::unique_ptr<char[]> data =
        std::make_unique<char[]>(sizeof(peer_fragment_request_t) + ids.size() * sizeof(fragment_id_t));
    socket_buffer_t send_buffer((byte *)data.get(),
                                sizeof(peer_fragment_request_t) + ids.size() * sizeof(fragment_id_t));

    peer_fragment_request_t *request = (peer_fragment_request_t *)data.get();
    request->count = ids.size();
    request->priority = priority;
    request->type = peer_msg_type::fragment_request;
    send_buffer.expect().origin_length();
    endian::cast_inplace(*request, send_buffer);
    send_buffer.walk_step(sizeof(peer_fragment_request_t));
    int i = 0;
    for (auto id : ids)
    {
        request->ids[i] = id;
        endian::cast_inplace(request->ids[i++], send_buffer);
        send_buffer.walk_step(sizeof(fragment_id_t));
    }

    send_buffer.expect().origin_length();

    co::await(rudp_awrite, &udp, send_buffer, target->remote_address);
}

void peer_t::update_metainfo(u64 key, peer_info_t *target)
{
    peer_request_metainfo_t request;

    socket_buffer_t send_buffer((byte *)&request, sizeof(request));

    request.key = key;
    request.type = peer_msg_type::get_meta;

    send_buffer.expect().origin_length();
    endian::cast_inplace(request, send_buffer);

    co::await(rudp_awrite, &udp, send_buffer, target->remote_address);
}

void peer_t::pull_meta_data(peer_info_t *peer, u64 key)
{
    peer->meta_request_queue.emplace(key);
    sendable_peers.push(peer);
}

void peer_t::pull_fragment_from_peer(peer_info_t *peer, std::vector<fragment_id_t> fid, u8 priority)
{
    peer->frag_request_queue.emplace(std::move(fid), priority);
    sendable_peers.push(peer);
}

void peer_t::send_fragment_to_peer(peer_info_t *peer, fragment_id_t fid, socket_buffer_t buffer)
{
    peer->fragment_send_queue.push(std::make_tuple(fid, std::move(buffer)));
    sendable_peers.push(peer);
}

void peer_t::send_meta_data_to_peer(peer_info_t *peer, u64 key, socket_buffer_t buffer)
{
    peer->meta_send_queue.push(std::make_tuple(key, std::move(buffer)));
    sendable_peers.push(peer);
}

} // namespace net::p2p