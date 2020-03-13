/**
* \file execute_context.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief The execution context is a CPU execution context, including independent stack and register information. It
should be bound to a random event loop and always run in the event loop.
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
    /// this sleep can be interrupt by event. Check return value
    ///
    ///\param ms sleep time in microsecond
    ///\return return how much time we sleep, The return value may be smaller than the parameter when an event occurs
    /// during sleep
    microsecond_t sleep(microsecond_t ms);
    void stop();

    void stop_for(microsecond_t ms, std::function<void()> func);
    void stop_for(microsecond_t ms);

    event_loop_t *get_loop() const { return loop; }
    void set_loop(event_loop_t *loop) { this->loop = loop; }

    /// Rerun the coroutine and push it to the dispatcher queue
    void start();
    /// Rerun the coroutine and push it to the dispatcher queue. Call func before resume coroutine.
    void start_with(std::function<void()> func);

    /// start coroutine and set function. Push it to dispatcher queue
    ///
    ///\param func the startup function to run.
    void run(std::function<void()> func);

    /// wake up loop to execute coroutine
    void wake_up_thread();

    execute_context_t();
    ~execute_context_t();
};

} // namespace net
