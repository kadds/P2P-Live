#pragma once
#include "net/event.hpp"
#include "net/net.hpp"
#include "net/socket_addr.hpp"
#include <functional>

void init_peer(net::u64 sid, net::socket_addr_t ts_server_addr, net::microsecond_t timeout);
void send_data(void *buffer, int size, int channel, net::u64 fragment_id);
void send_meta_info(void *buffer, int size, int channel, int key);

bool is_connect();

// return true for reconnect
void on_connection_error(std::function<bool(net::connection_state)>);
void on_edge_server_prepared(std::function<void()>);

void close_peer();