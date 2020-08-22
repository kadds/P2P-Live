/**
* \file event.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief Includes event demultiplexer interface, event handler interface, event loop and event context.
* \version 0.1
* \date 2020-03-13
*
* @copyright Copyright (c) 2020.
This file is part of P2P-Live.

P2P-Live is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

P2P-Live is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with P2P-Live. If not, see <http: //www.gnu.org/licenses/>.
*
*/

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

using event_type_t = unsigned long;

/// different handle types on different platforms
using handle_t = int;

namespace event_type
{
enum : event_type_t
{
    readable = 1,
    writable = 2,
    error = 4,
};
};
/// forward declaration
class socket_t;
class event_context_t;
class event_loop_t;
class execute_context_t;
class event_fd_handler_t;

enum event_strategy
{
    select,
    epoll,
    /// TODO: Encapsulation of IOCP
    IOCP,
    AUTO,
};

/// Demultiplexer
/// Wait and select an available event, while specifying the wait timeout
///\note Different implementations may have different thread safety for different platforms, please do not always
/// consider it to be thread safe.
class event_demultiplexer
{
  public:
    /// register event on handle
    ///
    ///\param handle the socket handle to listen
    ///\param type which event to listen. readable, writable, error.
    ///
    ///\note adding an already added event will not affect
    virtual void add(handle_t handle, event_type_t type) = 0;

    /// listen and return a socket handle which happends event
    ///
    ///\param type a pointer to event_type. return socket event type
    ///\param timeout maximum time to wait. If an error occurs, the parameter is set to 0
    ///\return socket happends event, return 0 for error, just recall it
    virtual handle_t select(event_type_t *type, microsecond_t *timeout) = 0;

    /// unregister event on handle
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
    /// handle event
    ///\param context event context
    ///\param event_type which event type is distributed.
    virtual void on_event(event_context_t &, event_type_t) = 0;
    virtual ~event_handler_t(){};
};

/// event loop
/// a loop per thread
/// all event is generate by demultiplexer. it just fetch events and distribute event to event handler and run into
/// event coroutines.
///\note this class should be created by event context, don't create it by yourself.
class event_loop_t
{
  private:
    using event_handle_map_t = std::unordered_map<handle_t, event_handler_t *>;
    friend class event_context_t;

    bool is_exit;
    int exit_code;

    event_demultiplexer *demuxer;
    /// map handle -> event handler
    event_handle_map_t event_map;
    /// lock the event map
    lock::spinlock_t lock;

    event_context_t *context;
    std::unique_ptr<time_manager_t> time_manager;

    execute_thread_dispatcher_t dispatcher;

#ifndef OS_WINDOWS
    ///  only for wake up demuxer
    std::unique_ptr<event_fd_handler_t> wake_up_event_handler;
#endif

  private:
    void add_socket(socket_t *socket_t);
    void remove_socket(socket_t *socket_t);
    void set_demuxer(event_demultiplexer *demuxer);
    void set_context(event_context_t *context) { this->context = context; }

    event_demultiplexer *get_demuxer() const { return demuxer; }

  private:
    /// run loop util call exit
    int run();

  public:
    event_loop_t(microsecond_t precision);
    ~event_loop_t();

    event_loop_t(const event_loop_t &) = delete;
    event_loop_t &operator=(const event_loop_t &) = delete;

    /// exit event loop with code.
    void exit(int code);
    /// get workload
    int load_factor();

    /// register event 'type' on 'handle', call it repeatedly is allowed
    event_loop_t &link(handle_t handle, event_type_t type);
    /// unregister event 'type' on 'handle'
    event_loop_t &unlink(handle_t handle, event_type_t type);

    /// map handle -> handler
    /// thread-safety
    void add_event_handler(handle_t handle, event_handler_t *handler);
    void remove_event_handler(handle_t handle, event_handler_t *handler);

    timer_registered_t add_timer(timer_t timer);
    void remove_timer(timer_registered_t);

    execute_thread_dispatcher_t &get_dispatcher();

    /// get event loop current thread.
    ///\note don't call it before run event_context in current thread.
    static event_loop_t &current();

    /// wake up if event loop is sleeping.
    void wake_up();
};

/// event context
/// unique global context in an application
class event_context_t
{
    /// demultiplexing strategy
    event_strategy strategy;

    std::shared_mutex loop_mutex;
    std::vector<event_loop_t *> loops;

    /// sync when exit loops
    std::mutex exit_mutex;
    std::condition_variable cond;
    std::atomic_int loop_counter;

    /// timer precistion
    microsecond_t precision;

    /// init event loop in current thread
    void do_init();

  public:
    event_context_t(event_strategy strategy, microsecond_t precision = timer_min_precision);
    /// destroy all loops
    ///\note Wait for all loops to be destroyed and return
    ~event_context_t();

    void add_executor(execute_context_t *exectx);
    void add_executor(execute_context_t *exectx, event_loop_t *loop);
    void remove_executor(execute_context_t *exectx);

    /// select a event loop which is minimum work load
    event_loop_t &select_loop();

    /// Inform exit all event loop with exit code
    ///\note return immediately
    void exit_all(int code);
    /// run event loop in current thread
    ///\note called for each thread to run the event loop in each thread.
    int run();
};

} // namespace net