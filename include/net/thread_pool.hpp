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
