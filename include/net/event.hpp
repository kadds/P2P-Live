#pragma once
#include "execute_dispatcher.hpp"
#include "lock.hpp"
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
class execute_context_t;
using event_type_t = unsigned long;
namespace event_type
{
enum : event_type_t
{
    readable = 1,
    writable = 2,
    error = 4,

    /// extend event

};
};

class socket_t;
class event_context_t;
class event_loop_t;

enum event_strategy
{
    select,
    epoll,
    IOCP,
    best,
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

class event_handler_t
{
  public:
    virtual void on_event(event_context_t &, event_type_t) = 0;
    virtual ~event_handler_t(){};
};

using event_handle_map_t = std::unordered_map<handle_t, event_handler_t *>;
using socket_event_loop_map_t = std::unordered_map<handle_t, event_loop_t *>;

class event_fd_handler_t;
/// event loop
/// a loop pre thread
/// all event is generate by demultiplexer. event loop just fetch event and distribute event run into event coroutine.
class event_loop_t
{
    bool is_exit;
    int exit_code;
    event_demultiplexer *demuxer;
    event_handle_map_t event_map;
    friend class event_context_t;
    event_context_t *context;
    std::unique_ptr<time_manager_t> time_manager;
    execute_thread_dispatcher_t dispatcher;
    lock::spinlock_t lock;

    std::unique_ptr<event_fd_handler_t> wake_up_event_handler;

  private:
    void add_socket(socket_t *socket_t);
    void remove_socket(socket_t *socket_t);
    void set_demuxer(event_demultiplexer *demuxer);
    void set_context(event_context_t *context) { this->context = context; }

    event_demultiplexer *get_demuxer() const { return demuxer; }

  public:
    event_loop_t(microsecond_t precision);
    event_loop_t(std::unique_ptr<time_manager_t> time_manager);
    ~event_loop_t();
    event_loop_t(const event_loop_t &) = delete;
    event_loop_t &operator=(const event_loop_t &) = delete;

    int run();
    void exit(int code);
    int load_factor();

    event_loop_t &link(handle_t handle, event_type_t type);
    event_loop_t &unlink(handle_t handle, event_type_t type);

    void add_event_handler(handle_t handle, event_handler_t *handler);
    void remove_event_handler(handle_t handle, event_handler_t *handler);

    timer_registered_t add_timer(timer_t timer);
    void remove_timer(timer_registered_t);

    execute_thread_dispatcher_t &get_dispatcher();

    static event_loop_t &current();

    void wake_up();
};

/// event context
/// unique global context in an application
class event_context_t
{
    event_strategy strategy;
    std::shared_mutex loop_mutex;
    std::vector<event_loop_t *> loops;
    std::mutex exit_mutex;
    std::condition_variable cond;
    std::atomic_int loop_counter;
    socket_event_loop_map_t map;
    std::shared_mutex map_mutex;
    microsecond_t precision;

    void do_init();

  public:
    event_context_t(event_strategy strategy, microsecond_t precision = timer_min_precision);
    ~event_context_t();
    void add_executor(execute_context_t *exectx);
    void add_executor(execute_context_t *exectx, event_loop_t *loop);
    void remove_executor(execute_context_t *exectx);

    event_loop_t &select_loop();

    void exit_all(int code);
    int run();
};

} // namespace net