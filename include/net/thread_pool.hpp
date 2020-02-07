#pragma once
#include "co.hpp"
#include <condition_variable>
#include <functional>
#include <future>
namespace net
{
class thread_pool_t
{
  private:
  public:
    ///\param count thread count in pool
    thread_pool_t(int count);

    thread_pool_t(const thread_pool_t &) = delete;
    thread_pool_t &operator=(const thread_pool_t &) = delete;

    ~thread_pool_t();

    /// commit a task to thread pool
    ///
    ///\param task
    ///\return return async result which can wait by coroutine 'await' to get the result
    template <typename Ret> co::async_result_t<Ret> commit(std::function<Ret()> task) {}

    /// return idle thread count
    int get_idles() const;

    /// return working load in the thread pool. [0, 100] -> load
    int get_work_load() const;
    /// return true if there are no tasks in task queue and no threads are running tasks.
    bool empty() const;
};

} // namespace net
