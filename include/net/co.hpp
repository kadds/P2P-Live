/**
* \file co.hpp
* \author kadds (itmyxyf@gmail.com)
* \brief This is a encapsulation with a stackfull coroutine for the boost library. It consists of a series of coroutine
chains and runs in them.
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
#include "execute_context.hpp"
#include "net.hpp"
#include <boost/context/fiber.hpp>
#include <functional>
#include <future>
#include <list>

namespace net::co
{
namespace ctx = boost::context;

template <typename T> class async_result_t
{
    union u_t
    {
        T impl_data;
        u_t(T impl_data)
            : impl_data(impl_data)
        {
        }
        u_t() {}
    } u;

    bool ok;

  public:
    async_result_t(T t)
        : u(t)
        , ok(true)
    {
    }

    async_result_t()
        : ok(false)
    {
    }
    /// get result
    ///\note undefined when task is not completed
    T operator()() { return u.impl_data; }
    bool is_finish() { return ok; };
};

class coroutine_t;

/// the main coroutine
/// coroutine of the main thread, save the default stack information of the thread
thread_local inline coroutine_t *co_cur = nullptr;

ctx::fiber &&co_wrapper(ctx::fiber &&sink, coroutine_t *co);
ctx::fiber &&co_reschedule_wrapper(ctx::fiber &&sink, coroutine_t *co, std::function<void()> func);

/// throw it when wants to stop coroutine
class coroutine_stop_exception
{
};

class coroutine_t
{
    /// boost fiber
    ctx::fiber context;
    /// entry function
    std::function<void()> func;
    /// previous coroutine
    /// make up a list chain
    coroutine_t *prev;
    friend ctx::fiber &&co_wrapper(ctx::fiber &&sink, coroutine_t *co);
    friend ctx::fiber &&co_reschedule_wrapper(ctx::fiber &&sink, coroutine_t *co, std::function<void()> func);

    execute_context_t *econtext;
    bool is_stop;
    /// Don't create in the stack
    coroutine_t(std::function<void()> f)
        : context(std::bind(co_wrapper, std::placeholders::_1, this))
        , func(f)
        , prev(nullptr)
        , is_stop(false){};

  public:
    coroutine_t(const coroutine_t &) = delete;
    coroutine_t &operator=(const coroutine_t &) = delete;

    static coroutine_t *create(std::function<void()> f)
    {
        coroutine_t *co = new coroutine_t(f);
        return co;
    };
    /**
     * \brief return current coroutine
     *
     * \return coroutine_t*
     */
    static coroutine_t *current() { return co_cur; }

    static bool in_main_coroutine() { return co_cur == nullptr; }

    static bool in_coroutine(coroutine_t *co) { return co_cur == co; }

    static void remove(coroutine_t *c) { delete c; }

    /// XXX: Maybe there is a better way to sleep in coroutines
    /// It invalidates the SOLID principle
    execute_context_t *get_execute_context() { return econtext; }

    void set_execute_context(execute_context_t *ec) { econtext = ec; }

    // switch to this
    void resume()
    {
        if (is_stop)
            return;
        prev = co_cur;

        co_cur = this;

        context = std::move(context).resume();
    }

    // switch to this and call func
    void resume_with(std::function<void()> func)
    {
        if (is_stop)
            return;
        prev = co_cur;

        co_cur = this;
        context = std::move(context).resume_with(std::bind(co_reschedule_wrapper, std::placeholders::_1, this, func));
    }

    static void yield()
    {
        coroutine_t *cur = current();
        if (cur)
        {
            co_cur = cur->prev;
            auto next = cur;
            cur->context = std::move(cur->context).resume();
            if (next->is_stop)
                remove(next);
            return;
        }
        else
        {
            throw std::exception();
        }
    }

    static void yield(std::function<void()> func)
    {
        coroutine_t *cur = current();
        if (cur)
        {
            co_cur = cur->prev;
            auto next = cur;
            cur->context =
                std::move(cur->context).resume_with(std::bind(co_reschedule_wrapper, std::placeholders::_1, cur, func));
            if (next->is_stop)
                remove(next); // delay removal of coroutine
            return;
        }
        else
        {
            throw std::exception();
        }
    }
    /// When returning to the previous coroutine, the stop flag is set and destroyed.
    void stop() { is_stop = true; }
};

class paramter_t
{
    /// how many times called
    int times;
    //. stop immediately because timeout
    bool stop;
    void *user_ptr;

  public:
    paramter_t()
        : times(0)
        , stop(false)
        , user_ptr(nullptr){};

    bool is_stop() const { return stop; }
    int get_times() const { return times; }
    void set_user_ptr(void *ptr) { user_ptr = ptr; }
    void *get_user_ptr() const { return user_ptr; }
    void stop_wait() { stop = true; }
    void add_times() { times++; }
};

/// async wait
///
///\tparam func function to async wait
///\tparam args function args request
///\return return function result when async wait ok
///\note All Func with coroutine tag is not reentrant. Don't wait for function calls with the same parameters at the
/// same time.
template <typename Func, typename... Args> inline static auto await(Func func, Args &&... args)
{
    paramter_t param;
    while (1)
    {
        auto ret = func(param, std::forward<Args>(args)...);
        if (ret.is_finish())
        {
            return ret();
        }
        param.add_times();
        coroutine_t::yield();
    }
}

/// async wait timeout
///
///\tparam func function to async wait
///\tparam args function args request
///\param span microseconds for maximum timeout
///\return return function result when async wait ok
///\note All Func with coroutine tag is not reentrant. Don't wait for function calls with the same parameters at the
/// same time.
template <typename Func, typename... Args>
inline static auto await_timeout(microsecond_t span, Func func, Args &&... args)
{
    paramter_t param;
    auto co = coroutine_t::current();
    while (1)
    {
        auto ret = func(param, std::forward<Args>(args)...);
        if (ret.is_finish())
        {
            return ret();
        }
        span = co->get_execute_context()->sleep(span);
        if (span == 0)
            param.stop_wait();
        param.add_times();
    }
}

} // namespace net::co