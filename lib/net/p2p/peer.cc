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

void peer_t::bind_udp()
{
    udp.on_new_connection(std::bind(&peer_t::main, this, std::placeholders::_1));
    udp.on_unknown_packet([this](socket_addr_t addr) {
        if (peers.find(addr) != peers.end()) // if peer is registed. add udp connection
        {
            udp.add_connection(addr, 0, make_timespan(disconnect_tick));
            for (auto c : channels)
                udp.add_connection(addr, c, make_timespan(disconnect_tick));

            return true;
        }
        return false;
    });
}

void peer_t::bind(event_context_t &context)
{
    udp.bind(context);
    bind_udp();
}

void peer_t::bind(event_context_t &context, socket_addr_t addr_to_bind, bool reuse_addr)
{
    udp.bind(context, addr_to_bind, reuse_addr);
    bind_udp();
}

void peer_t::accept_channels(const std::vector<channel_t> &channels) { this->channels = channels; }

peer_info_t *peer_t::add_peer()
{
    auto peer = std::make_unique<peer_info_t>();
    peer->sid = 0;
    peer->has_connect = false;
    auto ptr = peer.get();
    noconnect_peers.emplace_back(std::move(peer));
    return ptr;
}

void peer_t::send_fragments(fragment_id_t fid, socket_buffer_t buffer, rudp_connection_t conn)
{
    socket_buffer_t send_buffer(udp.get_mtu());

    peer_fragment_respond_t *respond = (peer_fragment_respond_t *)send_buffer.get();
    respond->type = peer_msg_type::fragment_respond;
    respond->fid = fid;
    respond->frame_size = buffer.get_length();
    /// send first fragment
    int len = std::min(buffer.get_length(), udp.get_mtu() - sizeof(peer_fragment_respond_t));
    send_buffer.expect().length(sizeof(peer_fragment_respond_t) + len);
    endian::cast_inplace(*respond, send_buffer);
    memcpy(send_buffer.get() + sizeof(peer_fragment_respond_t), buffer.get(), len);
    co::await(rudp_awrite, &udp, conn, send_buffer);
    buffer.walk_step(len);
    send_buffer.expect().origin_length();
    /// send rest fragments
    peer_fragment_rest_respond_t *rsp = (peer_fragment_rest_respond_t *)send_buffer.get();
    rsp->type = peer_msg_type::fragment_respond_rest;

    while (buffer.get_length() > 0)
    {
        auto len = std::min((u32)buffer.get_length(), (u32)udp.get_mtu() - (u32)sizeof(peer_fragment_rest_respond_t));
        send_buffer.expect().length(len + sizeof(peer_fragment_rest_respond_t));
        memcpy(send_buffer.get() + sizeof(peer_fragment_rest_respond_t), buffer.get(), len);
        co::await(rudp_awrite, &udp, conn, send_buffer);
        buffer.walk_step(len);
    }
}

void peer_t::send_init(rudp_connection_t conn)
{
    peer_init_request_t req;
    req.type = peer_msg_type::init_request;
    req.sid = sid;
    socket_buffer_t buffer = socket_buffer_t::from_struct(req);
    buffer.expect().origin_length();
    endian::cast_inplace(req, buffer);
    co::await(rudp_awrite, &udp, conn, buffer);
}

void peer_t::async_do_write(peer_info_t *peer, int channel)
{
    auto conn = peer->channel[channel].conn;
    udp.run_at(conn, [this, peer, conn, channel]() {
        if (!peer->has_connect && channel == 0)
        {
            send_init(conn);
        }
        auto &queues = peer->channel[channel];
        if (!queues.frag_request_queue.empty())
        {
            while (!queues.frag_request_queue.empty())
            {
                auto val = queues.frag_request_queue.front();
                queues.frag_request_queue.pop();
                update_fragments(std::get<std::vector<fragment_id_t>>(val), std::get<u8>(val), conn);
            }
        }
        else if (!queues.fragment_send_queue.empty())
        {
            while (!queues.fragment_send_queue.empty())
            {
                auto val = queues.fragment_send_queue.front();
                queues.fragment_send_queue.pop();
                send_fragments(std::get<fragment_id_t>(val), std::move(std::get<socket_buffer_t>(val)), conn);
            }
        }
        else if (!queues.meta_request_queue.empty())
        {
            while (!queues.meta_request_queue.empty())
            {
                auto key = queues.meta_request_queue.front();
                queues.meta_request_queue.pop();
                update_metainfo(key, conn);
            }
        }
        else if (!queues.meta_send_queue.empty())
        {
            while (!queues.meta_send_queue.empty())
            {
                auto val = queues.meta_send_queue.front();
                queues.meta_send_queue.pop();
                send_metainfo(std::get<u64>(val), std::move(std::get<socket_buffer_t>(val)), conn);
            }
        }
    });
}

peer_info_t *peer_t::find_peer(socket_addr_t addr)
{
    auto it = peers.find(addr);
    if (it != peers.end())
        return it->second.get();
    return nullptr;
}

void peer_t::main(rudp_connection_t conn)
{
    /// 1472 is udp MSS
    std::unique_ptr<char[]> data = std::make_unique<char[]>(1472);
    socket_buffer_t recv_buffer((byte *)data.get(), 1472);
    int channel = conn.channel;
    auto peer = find_peer(conn.address);
    if (peer == nullptr)
        return;
    peer->channel[conn.channel].conn = conn;
    async_do_write(peer, conn.channel);

    while (1)
    {
        recv_buffer.expect().origin_length();
        auto ret = co::await(rudp_aread, &udp, conn, recv_buffer);

        u8 type = recv_buffer.get()[0];
        if (type == peer_msg_type::init_request)
        {
            if (recv_buffer.get_length() < sizeof(peer_init_request_t))
                continue;
            peer_init_request_t *request = (peer_init_request_t *)data.get();
            endian::cast_inplace(*request, recv_buffer);
            if ((sid != 0 && request->sid != 0) && request->sid != sid)
                continue;

            peer->last_ping = get_timestamp();
            peer->sid = sid;

            if (peer->has_connect)
                continue;

            peer_init_respond_t respond;
            respond.type = peer_msg_type::init_respond;
            respond.first_data_id = 0;
            respond.last_data_id = 0;
            recv_buffer.expect().length(sizeof(respond));
            endian::save_to(respond, recv_buffer);
            co::await(rudp_awrite, &udp, conn, recv_buffer);
        }
        else if (type == peer_msg_type::init_respond)
        {
            if (recv_buffer.get_length() < sizeof(peer_init_respond_t))
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
            if (recv_buffer.get_length() < sizeof(peer_request_metainfo_t))
                continue;
            peer->last_ping = get_timestamp();
            peer_request_metainfo_t *request = (peer_request_metainfo_t *)data.get();
            if (meta_handler)
                meta_handler(*this, peer, request->key, conn.channel);
        }
        else if (type == peer_msg_type::meta_respond)
        {
            if (recv_buffer.get_length() < sizeof(peer_meta_respond_t))
                continue;
            peer_meta_respond_t *respond = (peer_meta_respond_t *)data.get();
            endian::cast_inplace(*respond, recv_buffer);
            recv_buffer.walk_step(sizeof(peer_meta_respond_t));
            if (meta_recv_handler)
                meta_recv_handler(*this, peer, recv_buffer, respond->key, conn.channel);
        }
        else if (type == peer_msg_type::fragment_request)
        {
            if (recv_buffer.get_length() < sizeof(peer_fragment_request_t))
                continue;
            peer_fragment_request_t *request = (peer_fragment_request_t *)data.get();
            if (request->count * sizeof(fragment_id_t) + sizeof(peer_fragment_request_t) > recv_buffer.get_length())
                continue;

            if (meta_handler)
            {
                recv_buffer.walk_step(sizeof(peer_fragment_request_t));
                if (fragment_handler)
                    for (auto i = 0; i < request->count; i++)
                    {
                        endian::cast_inplace(request->ids[i], recv_buffer);
                        fragment_handler(*this, peer, request->ids[i], conn.channel);
                        recv_buffer.walk_step(sizeof(fragment_id_t));
                    }
            }
        }
        else if (type == peer_msg_type::fragment_respond)
        {
            auto &chq = peer->channel[conn.channel];
            if (chq.fragment_recv_buffer_cache.get_base_ptr() == nullptr)
            {
                // new fragment
                if (recv_buffer.get_length() < sizeof(peer_fragment_respond_t))
                    continue;
                peer_fragment_respond_t *frag_respond = (peer_fragment_respond_t *)data.get();
                endian::cast_inplace(*frag_respond, recv_buffer);
                if (frag_respond->frame_size > 0x1000000) /// XXX: 16MB too large
                {
                    // close peer
                }
                chq.fragment_recv_id = frag_respond->fid;

                chq.fragment_recv_buffer_cache = socket_buffer_t(frag_respond->frame_size);
                chq.fragment_recv_buffer_cache.expect().origin_length();
                u32 len = std::min(frag_respond->frame_size,
                                   (u32)recv_buffer.get_length() - (u32)sizeof(peer_fragment_respond_t));

                memcpy(chq.fragment_recv_buffer_cache.get(), recv_buffer.get() + sizeof(peer_fragment_respond_t), len);
                chq.fragment_recv_buffer_cache.walk_step(len);
            }
            if (chq.fragment_recv_buffer_cache.get_length() == 0)
            {
                chq.fragment_recv_buffer_cache.expect().origin_length();
                if (fragment_recv_handler)
                    fragment_recv_handler(*this, peer, chq.fragment_recv_buffer_cache, chq.fragment_recv_id,
                                          conn.channel);
                chq.fragment_recv_buffer_cache = {};
            }
        }
        else if (type == peer_msg_type::fragment_respond_rest)
        {
            auto &chq = peer->channel[channel];
            if (chq.fragment_recv_buffer_cache.get_base_ptr() == nullptr)
            {
            }
            else
            {
                if (recv_buffer.get_length() < sizeof(peer_fragment_rest_respond_t))
                    continue;
                peer_fragment_rest_respond_t *frag_respond = (peer_fragment_rest_respond_t *)data.get();
                endian::cast_inplace(*frag_respond, recv_buffer);

                u32 len = std::min((u32)chq.fragment_recv_buffer_cache.get_length(),
                                   (u32)recv_buffer.get_length() - (u32)sizeof(peer_fragment_rest_respond_t));
                memcpy(chq.fragment_recv_buffer_cache.get(), recv_buffer.get() + sizeof(peer_fragment_rest_respond_t),
                       len);
                chq.fragment_recv_buffer_cache.walk_step(len);
            }
            if (chq.fragment_recv_buffer_cache.get_length() == 0)
            {
                chq.fragment_recv_buffer_cache.expect().origin_length();
                if (fragment_recv_handler)
                    fragment_recv_handler(*this, peer, chq.fragment_recv_buffer_cache, chq.fragment_recv_id,
                                          conn.channel);
                chq.fragment_recv_buffer_cache = {};
            }
        }
    }
}

void peer_t::heartbeat(rudp_connection_t conn)
{
    u8 type = peer_msg_type::heart;
    socket_buffer_t buffer(&type, sizeof(type));
    buffer.expect().origin_length();
    endian::cast_inplace(type, buffer);
    co::await(rudp_awrite, &udp, conn, buffer);
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

            /// inactive timeout
            udp.add_connection(remote_addr, 0, make_timespan(disconnect_tick));
            for (auto c : channels)
            {
                udp.add_connection(remote_addr, c, make_timespan(disconnect_tick));
            }
            return;
        }
    }
}

void peer_t::disconnect(peer_info_t *peer)
{
    auto it = peers.find(peer->remote_address);
    if (it != peers.end())
    {
        udp.remove_connection(peer->remote_address, 0);
        for (auto c : channels)
            udp.remove_connection(peer->remote_address, c);
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

void peer_t::update_fragments(std::vector<fragment_id_t> ids, u8 priority, rudp_connection_t conn)
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

    co::await(rudp_awrite, &udp, conn, send_buffer);
}

void peer_t::update_metainfo(u64 key, rudp_connection_t conn)
{
    peer_request_metainfo_t request;

    socket_buffer_t send_buffer = socket_buffer_t::from_struct(request);

    request.key = key;
    request.type = peer_msg_type::get_meta;

    send_buffer.expect().origin_length();
    endian::cast_inplace(request, send_buffer);

    co::await(rudp_awrite, &udp, conn, send_buffer);
}

void peer_t::send_metainfo(u64 key, socket_buffer_t buffer, rudp_connection_t conn)
{
    socket_buffer_t send_buffer(buffer.get_length() + sizeof(peer_meta_respond_t));
    peer_meta_respond_t *respond = (peer_meta_respond_t *)send_buffer.get();
    send_buffer.expect().origin_length();
    respond->type = peer_msg_type::meta_respond;
    respond->key = key;
    endian::cast_inplace(*respond, send_buffer);
    send_buffer.expect().origin_length();
    memcpy(send_buffer.get() + sizeof(peer_meta_respond_t), buffer.get(), buffer.get_length());

    co::await(rudp_awrite, &udp, conn, send_buffer);
}

void peer_t::pull_meta_data(peer_info_t *peer, u64 key, channel_t channel)
{
    peer->channel[channel].meta_request_queue.emplace(key);
    async_do_write(peer, channel);
}

void peer_t::pull_fragment_from_peer(peer_info_t *peer, std::vector<fragment_id_t> fid, channel_t channel, u8 priority)
{
    peer->channel[channel].frag_request_queue.emplace(std::move(fid), priority);
    async_do_write(peer, channel);
}

void peer_t::send_fragment_to_peer(peer_info_t *peer, fragment_id_t fid, channel_t channel, socket_buffer_t buffer)
{
    peer->channel[channel].fragment_send_queue.push(std::make_tuple(fid, std::move(buffer)));
    async_do_write(peer, channel);
}

void peer_t::send_meta_data_to_peer(peer_info_t *peer, u64 key, channel_t channel, socket_buffer_t buffer)
{
    peer->channel[channel].meta_send_queue.push(std::make_tuple(key, std::move(buffer)));
    async_do_write(peer, channel);
}

} // namespace net::p2p