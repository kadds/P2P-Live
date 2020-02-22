#pragma once
#include <atomic>

namespace net::lock
{
class spinlock_t
{
    std::atomic_bool flag;

  public:
    spinlock_t()
        : flag(false)
    {
    }
    ~spinlock_t() {}
    void lock()
    {
        bool old = false;
        do
        {
            while (flag)
            {
            }
            old = false;
        } while (!flag.compare_exchange_strong(old, true, std::memory_order_acquire));
    }

    void unlock() { flag.store(false, std::memory_order_release); }
};

class rw_lock_t
{
    std::atomic_ullong flag;

  public:
    rw_lock_t()
        : flag(0)
    {
    }

    void rlock()
    {
        unsigned long long old;
        do
        {
            while (flag & (1ull << 63)) /// is writing
            {
            }

            old = flag & ((1ul << 63) - 1);
        } while (!flag.compare_exchange_strong(old, old + 1, std::memory_order_acquire));
    }

    void runlock()
    {
        unsigned long long old;
        do
        {
            old = flag & ((1ul << 63) - 1);
        } while (!flag.compare_exchange_strong(old, old - 1, std::memory_order_acquire));
    }

    void lock()
    {
        unsigned long long old;
        do
        {
            while (flag != 0) /// is reading
            {
            }

            old = 0;
        } while (!flag.compare_exchange_strong(old, (1ull << 63), std::memory_order_acquire));
    }

    void unlock() { flag.store(0, std::memory_order_release); }
};

template <typename T> struct shared_lock_guard
{
    T &ref;
    shared_lock_guard(T &ref)
        : ref(ref)
    {
        ref.rlock();
    }
    ~shared_lock_guard() { ref.runlock(); }
};

template <typename T> struct lock_guard
{
    T &ref;
    lock_guard(T &ref)
        : ref(ref)
    {
        ref.lock();
    }
    ~lock_guard() { ref.unlock(); }
};

} // namespace net::lock