#pragma once
#include "net/net.hpp"
#include "net/socket_addr.hpp"
#include "net/socket_buffer.hpp"
#include "net/timer.hpp"

void init_peer(net::u64 sid, net::socket_addr_t tserver_addr, net::microsecond_t timeout);

void close_peer();

void on_connect_error(std::function<bool()>);

void enable_share();

void disable_share();

void cancel_fragment(net::u64 fid, int channel);

net::socket_buffer_t get_fragment(net::u64 fid, int channel);

net::socket_buffer_t get_meta(int key, int channel);
