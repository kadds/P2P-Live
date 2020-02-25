#pragma once
#include "timer.hpp"

namespace net
{
namespace co
{
class coroutine_t;
} // namespace co

class execute_thread_dispatcher_t;
class event_loop_t;

class execute_context_t
{
    co::coroutine_t *co;
    event_loop_t *loop;
    friend class event_context_t;
    friend class execute_thread_dispatcher_t;
    timer_registered_t timer;

  public:
    microsecond_t sleep(microsecond_t ms);
    void stop();

    void stop_for(microsecond_t ms, std::function<void()> func);
    void stop_for(microsecond_t ms);

    event_loop_t *get_loop() const { return loop; }
    void set_loop(event_loop_t *loop) { this->loop = loop; }

    void start();
    void start_with(std::function<void()> func);
    void run(std::function<void()> func);

    /// wake up loop to execute coroutine
    void wake_up_thread();

    execute_context_t();
    ~execute_context_t();
};

} // namespace net
