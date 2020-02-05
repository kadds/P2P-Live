#pragma once
#include "net.hpp"
#include "timer.hpp"
#include <functional>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace net
{
using handle_t = int;

using event_type_t = unsigned long;
namespace event_type
{
enum : event_type_t
{
    readable = 1,
    writable = 2,
    error = 4,

    /// extend event
    add = 32,
    remove = 64,
    tick = 128,

};
};

class socket_t;

class event_context_t;

enum class event_result
{
    ok,
    remove_handler,
};

using event_handler_t = std::function<event_result(event_context_t &context, event_type_t type, socket_t *socket)>;

struct event_handler_content_t
{
    event_handler_t handler;
    bool enable;
    event_handler_content_t(event_handler_t handler)
        : handler(handler)
        , enable(true)
    {
    }
};

class event_loop_t;

using socket_event_map_t = std::unordered_map<handle_t, std::list<event_handler_content_t>>;
using socket_handle_map_t = std::unordered_map<handle_t, socket_t *>;
using socket_event_loop_map_t = std::unordered_map<handle_t, event_loop_t *>;

enum event_strategy
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
    ///\param handle the socket handle to listen
    ///\param type which event to listen. readable, writable, error.
    ///
    virtual void add(handle_t handle, event_type_t type) = 0;

    /// listen and return a socket handle which happends event
    ///
    ///\param type a pointer to event_type. return socket event type
    ///\param timeout timeout
    ///\return socket happends event
    virtual handle_t select(event_type_t *type, microsecond_t *timeout) = 0;

    /// unregister socket on event type
    ///
    ///\param socket handle
    ///\param type unregister event type. readable, writable, error.
    ///
    virtual void remove(handle_t handle, event_type_t type) = 0;

    virtual ~event_demultiplexer(){};
};

class event_loop_t
{
    bool is_exit;
    int exit_code;
    event_demultiplexer *demuxer;
    socket_handle_map_t socket_map;
    friend class event_context_t;
    event_context_t *context;
    std::unique_ptr<time_manager_t> time_manager;

  private:
    void add_socket(socket_t *socket_t);
    void remove_socket(socket_t *socket_t);
    void set_demuxer(event_demultiplexer *demuxer) { this->demuxer = demuxer; }
    void set_context(event_context_t *context) { this->context = context; }

    event_demultiplexer *get_demuxer() const { return demuxer; }

  public:
    event_loop_t();
    event_loop_t(std::unique_ptr<time_manager_t> time_manager);

    int run();
    void exit(int code);
    int load_factor();

    event_loop_t &link(socket_t *socket_t, event_type_t type);
    event_loop_t &unlink(socket_t *socket_t, event_type_t type);
    void add_timer(timer_t timer);
};

class event_context_t
{
    event_strategy strategy;
    std::shared_mutex loop_mutex;
    std::vector<event_loop_t *> loops;
    socket_event_loop_map_t map;
    std::shared_mutex map_mutex;

  public:
    event_context_t(event_strategy strategy);
    event_loop_t &add_socket(socket_t *socket_t);
    event_loop_t *remove_socket(socket_t *socket_t);

    void add_event_loop(event_loop_t *loop);
    void remove_event_loop(event_loop_t *loop);
};

} // namespace net