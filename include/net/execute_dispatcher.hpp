/**
* \file execute_dispatcher.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief coroutine dispatcher via FIFO
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
#include "lock.hpp"
#include <functional>
#include <queue>
#include <tuple>
#include <unordered_set>

namespace net
{
class execute_context_t;

class execute_thread_dispatcher_t
{
    std::queue<std::tuple<execute_context_t *, std::function<void()>>> co_wait_for_resume;
    std::unordered_set<execute_context_t *> cancel_contexts;
    lock::spinlock_t lock;
    lock::spinlock_t cancel_lock;

  public:
    /// Not thread-safe and must be called by the event loop to execute the execute context in the queue.
    ///\note this function is called automatically in event loop.
    void dispatch();

    /// Thread-safe functions. Cancel an execute context
    void cancel(execute_context_t *econtext);
    /// Thread-safe functions. Add an execute context to the queue and set the wakeup function to execute
    void add(execute_context_t *econtext, std::function<void()> func);
};
} // namespace net
