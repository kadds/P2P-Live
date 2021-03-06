#include "net/thread_pool.hpp"
#include <atomic>
#include <functional>
#include <gtest/gtest.h>

TEST(ThreadPoolTest, BaseTest)
{
    int ok = false;
    {
        net::thread_pool_t pool(4);
        pool.commit([&ok]() { ok = 1; });
    }
    GTEST_ASSERT_EQ(ok, 1);
}

void calc(std::mutex &mutex, std::condition_variable &cv, std::atomic_int &c)
{
#ifndef OS_WINDOWS
    sleep(1);
#else
    _sleep(1);
#endif
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