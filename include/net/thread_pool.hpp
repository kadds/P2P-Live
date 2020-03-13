/**
* \file thread_pool.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief Simple thread pool implementation for high CPU load task runs
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
#include <atomic>
#include <condition_variable>
#include <functional>
#include <queue>
#include <thread>

namespace net
{

class thread_pool_t
{
  private:
    std::vector<std::thread> threads;
    mutable std::mutex mutex;
    std::condition_variable cond;
    std::queue<std::function<void()>> tasks;
    // exit flag
    bool exit;
    std::atomic_int counter;
    void wrapper();

  public:
    ///\param count thread count in pool
    thread_pool_t(int count);

    thread_pool_t(const thread_pool_t &) = delete;
    thread_pool_t &operator=(const thread_pool_t &) = delete;

    ///\note we wait all threads to exit at here
    ~thread_pool_t();

    /// commit a task to thread pool
    ///
    ///\param task to run
    ///\return none
    void commit(std::function<void()> task);

    /// return idle thread count
    int get_idles() const;

    /// return true if there are no tasks in task queue and no threads are running tasks.
    bool empty() const;
};

} // namespace net
