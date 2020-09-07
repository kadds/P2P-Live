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
    udp.on_new_connection(std::bind(&peer_t::pmain, this, std::placeholders::_1));
    udp.on_unknown_packet([this](socket_addr_t addr) {
        /// always accept
        if (peers.count(addr) == 0)
            peers.emplace(addr, std::move(std::make_unique<peer_info_t>()));

        udp.add_connection(addr, 0, make_timespan(disconnect_tick));
        for (auto c : channels)
            udp.add_connection(addr, c, make_timespan(disconnect_tick));

        return true;
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
    Package pkg;
    auto &rsp = *pkg.mutable_fragment_rsp();
    rsp.set_fragment_id(fid);
    rsp.set_length(buffer.get_length());
    rsp.set_data("");
    auto mtu = udp.get_mtu();
    auto base_len = mtu - pkg.ByteSizeLong() - 8;

    auto len = std::min(buffer.get_length(), base_len);
    rsp.set_data(buffer.get(), len);
    auto pkg_len = pkg.ByteSizeLong();
    if (pkg_len > mtu)
    {
        throw net_io_exception("length too large");
    }
    socket_buffer_t send_buffer(pkg_len);
    send_buffer.expect().origin_length();
    pkg.SerializeWithCachedSizesToArray(send_buffer.get());

    co::await(rudp_awrite, &udp, conn, send_buffer);
    buffer.walk_step(len);
    /// send rest fragments

    auto &rsp_rest = *pkg.mutable_fragment_rsp_rest();
    while (buffer.get_length() > 0)
    {
        len = std::min(buffer.get_length(), base_len);
        rsp_rest.set_data(buffer.get(), len);
        pkg_len = pkg.ByteSizeLong();
        if (pkg_len > mtu)
        {
            throw net_io_exception("length too large");
        }
        send_buffer.expect().length(pkg_len);
        pkg.SerializeWithCachedSizesToArray(send_buffer.get());
        co::await(rudp_awrite, &udp, conn, send_buffer);
        buffer.walk_step(len);
    }
}

void peer_t::send_init(rudp_connection_t conn)
{
    Package pkg;
    pkg.Clear();
    pkg.mutable_init_req()->set_sid(sid);
    socket_buffer_t buffer(pkg);
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

void peer_t::pmain(rudp_connection_t conn)
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
        Package pkg;
        Package new_pkg;
        pkg.ParseFromArray(recv_buffer.get(), recv_buffer.get_length());

        if (pkg.has_init_req())
        {
            auto &init_req = pkg.init_req();
            if ((sid != 0 && init_req.sid() != 0) && init_req.sid() != sid)
                continue;

            peer->last_ping = get_timestamp();
            peer->sid = sid;

            if (peer->has_connect)
                continue;
            auto &rsp = *new_pkg.mutable_init_rsp();
            rsp.set_fragment_id_beg(0);
            rsp.set_fragment_id_end(0);
            recv_buffer.expect().length(new_pkg.ByteSizeLong());
            new_pkg.SerializeWithCachedSizesToArray(recv_buffer.get());
            co::await(rudp_awrite, &udp, conn, recv_buffer);
        }
        else if (pkg.has_init_rsp())
        {
            peer->last_ping = get_timestamp();
            peer->has_connect = true;

            if (connect_handler)
                connect_handler(*this, peer);
        }
        else if (pkg.has_heart())
        {
            peer->last_ping = get_timestamp();
        }
        else if (pkg.has_meta_req())
        {
            peer->last_ping = get_timestamp();
            auto &meta_req = pkg.meta_req();
            if (meta_handler)
                meta_handler(*this, peer, meta_req.key(), conn.channel);
        }
        else if (pkg.has_meta_rsp())
        {
            auto &meta_rsp = pkg.meta_rsp();
            if (meta_recv_handler)
            {
                socket_buffer_t buf = socket_buffer_t::from_string(meta_rsp.value());
                buf.expect().origin_length();
                meta_recv_handler(*this, peer, buf, meta_rsp.key(), conn.channel);
            }
        }
        else if (pkg.has_fragment_req())
        {
            auto &fragment_req = pkg.fragment_req();

            if (meta_handler)
            {
                if (fragment_handler)
                {
                    for (auto i = 0; i < fragment_req.fragment_ids_size(); i++)
                    {
                        fragment_handler(*this, peer, fragment_req.fragment_ids(i), conn.channel);
                        recv_buffer.walk_step(sizeof(fragment_id_t));
                    }
                }
            }
        }
        else if (pkg.has_fragment_rsp())
        {
            auto &chq = peer->channel[conn.channel];
            if (chq.fragment_recv_buffer_cache.get_base_ptr() == nullptr)
            {
                // new fragment
                auto &fragment_rsp = pkg.fragment_rsp();
                if (fragment_rsp.length() > 0x1000000) /// XXX: 16MB too large
                {
                    // close peer
                }
                chq.fragment_recv_id = fragment_rsp.fragment_id();

                chq.fragment_recv_buffer_cache = socket_buffer_t(fragment_rsp.length());
                chq.fragment_recv_buffer_cache.expect().origin_length();
                u64 len = std::min(fragment_rsp.length(), fragment_rsp.data().size());

                memcpy(chq.fragment_recv_buffer_cache.get(), fragment_rsp.data().data(), len);
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
        else if (pkg.has_fragment_rsp_rest())
        {
            auto &chq = peer->channel[channel];
            if (chq.fragment_recv_buffer_cache.get_base_ptr() == nullptr)
            {
            }
            else
            {
                auto &fragment_rsp = pkg.fragment_rsp_rest();
                if (fragment_rsp.is_rst())
                {
                    chq.fragment_recv_buffer_cache = {};
                    continue;
                }

                auto len = std::min(chq.fragment_recv_buffer_cache.get_length(), fragment_rsp.data().size());
                memcpy(chq.fragment_recv_buffer_cache.get(), fragment_rsp.data().data(), len);
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
    Package pkg;
    auto &heart = *pkg.mutable_heart();

    socket_buffer_t send_buffer(pkg);
    co::await(rudp_awrite, &udp, conn, send_buffer);
}

void peer_t::connect_to_peer(peer_info_t *peer, socket_addr_t remote_peer_udp_addr)
{
    peer->remote_address = remote_peer_udp_addr;
    for (auto it = noconnect_peers.begin(); it != noconnect_peers.end(); ++it)
    {
        if (it->get() == peer)
        {
            peers.emplace(remote_peer_udp_addr, std::move(*it));
            noconnect_peers.erase(it);

            /// inactive timeout
            udp.add_connection(remote_peer_udp_addr, 0, make_timespan(disconnect_tick));
            for (auto c : channels)
            {
                udp.add_connection(remote_peer_udp_addr, c, make_timespan(disconnect_tick));
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

bool peer_t::has_connect_peer(socket_addr_t remote_peer_udp_addr) { return peers.count(remote_peer_udp_addr) > 0; }

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
    Package pkg;
    auto &req = *pkg.mutable_fragment_req();
    req.set_priority(priority);
    for (auto id : ids)
    {
        req.add_fragment_ids(id);
    }

    socket_buffer_t send_buffer(pkg);
    co::await(rudp_awrite, &udp, conn, send_buffer);
}

void peer_t::update_metainfo(u64 key, rudp_connection_t conn)
{
    Package pkg;
    auto &meta_req = *pkg.mutable_meta_req();
    meta_req.set_key(key);

    socket_buffer_t send_buffer(pkg);
    co::await(rudp_awrite, &udp, conn, send_buffer);
}

void peer_t::send_metainfo(u64 key, socket_buffer_t buffer, rudp_connection_t conn)
{
    Package pkg;
    auto &meta_rsp = *pkg.mutable_meta_rsp();
    meta_rsp.set_key(key);
    meta_rsp.set_value(buffer.to_string());

    socket_buffer_t send_buffer(pkg);
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