#include "net/thread_pool.hpp"
namespace net
{

void thread_pool_t::wrapper()
{
    while (!exit)
    {
        std::function<void()> task;
        counter++;
        {
            std::unique_lock<std::mutex> lock(mutex);
            cond.wait(lock, [this]() { return !this->empty() || exit; });
            if (exit)
                return;
            task = std::move(tasks.front());
            tasks.pop();
        }
        counter--;
        task();
    }
}

thread_pool_t::thread_pool_t(int count)
    : exit(false)
    , counter(0)
{
    for (auto i = 0; i < count; i++)
    {
        std::thread thread(std::bind(&thread_pool_t::wrapper, this));
        threads.emplace_back(std::move(thread));
    }
}

thread_pool_t::~thread_pool_t()
{
    exit = true;
    cond.notify_all();

    for (auto &i : threads)
    {
        i.join();
    }
}

void thread_pool_t::commit(std::function<void()> task)
{
    if (exit) // don't push task
        return;
    {
        std::unique_lock<std::mutex> lock(mutex);
        tasks.emplace(task);
    }
    cond.notify_one();
}

/// return idle thread count
int thread_pool_t::get_idles() const { return counter; }

/// return true if there are no tasks in task queue and no threads are running tasks.
bool thread_pool_t::empty() const { return tasks.empty(); }

} // namespace net