#pragma once
#include "net.hpp"
#include "socket.hpp"
#include <list>
#include <mutex>
#include <unordered_map>

namespace net
{
enum event_type
{
    readable,
    writable,
    error,
};

struct event_t
{
    event_type type;
    socket_t socket;

    event_t(event_type type, socket_t socket)
        : type(type)
        , socket(socket)
    {
    }
};

class event_base_t;

typedef void (*event_handler_t)(event_base_t &base, event_t event);

struct socket_t_cmp_t
{
    bool operator()(const socket_t &so1, const socket_t &so2) const
    {
        return so1.get_raw_handle() == so2.get_raw_handle();
    }
};

struct socket_t_hash_t
{
    std::size_t operator()(const socket_t &so1) const { return so1.get_raw_handle(); }
};

typedef std::unordered_map<event_type, std::list<event_handler_t>> socket_events_t;
typedef std::unordered_map<socket_t, socket_events_t, socket_t_hash_t, socket_t_cmp_t> socket_event_map_t;

enum event_base_strategy
{
    select,
    epoll,
    IOCP,
};

class event_demultiplexer
{
  public:
    /// register socket on event type
    ///
    ///\param socket the socket to listen
    ///\param type which event to listen. readable, writable, error.
    ///
    virtual void add(socket_t socket, event_type type) = 0;

    /// listen and return a socket which happends event
    ///
    ///\param type a pointer to event_type. return socket event type
    ///\return socket happends event
    virtual socket_t select(event_type *type) = 0;

    /// unregister socket on event type
    ///
    ///\param socket socket
    ///\param type unregister event type. readable, writable, error.
    ///
    virtual void remove(socket_t socket, event_type type) = 0;
};

class event_base_t
{
    bool is_exit;
    int exit_code;

    event_base_strategy strategy;
    event_demultiplexer *demuxer;
    socket_event_map_t event_map;
    std::mutex mutex;

  public:
    event_base_t(event_base_strategy strategy);
    void add_handler(event_type type, socket_t socket, event_handler_t handler);
    void remove_handler(event_type type, socket_t socket, event_handler_t handler);
    void remove_handler(event_type type, socket_t socket);
    void remove_handler(socket_t socket);

    void close_socket(socket_t socket);

    int run();
    void exit(int code);
};

class event_sub_base_t : public event_base_t
{
};

} // namespace net