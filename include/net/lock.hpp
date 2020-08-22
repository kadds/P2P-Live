/**
* \file lock.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief Implementation of spin locks and read-write locks
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
                /// XXX: sleep cpu here
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
    //. read lock
    void rlock()
    {
        unsigned long long old;
        do
        {
            while (flag & (1ull << 63)) /// is writing
            {
            }

            old = flag & ((1ull << 63) - 1);
        } while (!flag.compare_exchange_strong(old, old + 1, std::memory_order_acquire));
    }
    /// read unlock
    void runlock()
    {
        unsigned long long old;
        do
        {
            old = flag & ((1ull << 63) - 1);
        } while (!flag.compare_exchange_strong(old, old - 1, std::memory_order_acquire));
    }
    /// write lock
    void lock()
    {
        unsigned long long old;
        do
        {
            while (flag != 0) /// is reading or writing
            {
            }

            old = 0;
        } while (!flag.compare_exchange_strong(old, (1ull << 63), std::memory_order_acquire));
    }
    /// write unlock
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