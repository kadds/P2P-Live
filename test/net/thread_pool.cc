#include "net/thread_pool.hpp"
#include <atomic>
#include <functional>
#include <gtest/gtest.h>

TEST(ThreadPoolTest, BaseTest)
{
    int ok = false;
    std::mutex mutex;
    {
        mutex.lock();
        net::thread_pool_t poll(4);
        poll.commit([&ok, &mutex]() {
            ok = true;
            mutex.unlock();
        });
        mutex.lock();
    }
    mutex.unlock();
    GTEST_ASSERT_EQ(ok, true);
}

void calc(std::mutex &mutex, std::condition_variable &cv, std::atomic_int &c)
{
    sleep(1);
    std::unique_lock<std::mutex> lock(mutex);
    c--;
    cv.notify_one();
}

TEST(ThreadPoolTest, MultiThread)
{
    int x = 10;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic_int c = x;

    net::thread_pool_t poll(4);

    for (int i = 0; i < x; i++)
    {
        poll.commit(std::bind(calc, std::ref(mutex), std::ref(cv), std::ref(c)));
    }
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&c]() { return c == 0; });
}