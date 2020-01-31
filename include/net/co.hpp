#pragma once
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

    T operator()() { return u.impl_data; }
    bool is_finish() { return ok; };
};
class coroutine_t;

__thread inline coroutine_t *co_cur = nullptr;

constexpr int stack_size = 1 << 16;

ctx::fiber &&co_wrapper(ctx::fiber &&sink, coroutine_t *co);
ctx::fiber &&co_reschedule_wrapper(ctx::fiber &&sink, coroutine_t *co, std::function<void()> func);

class coroutine_t
{
    ctx::fiber context;
    std::function<void()> func;
    coroutine_t *prev;
    friend ctx::fiber &&co_wrapper(ctx::fiber &&sink, coroutine_t *co);
    friend ctx::fiber &&co_reschedule_wrapper(ctx::fiber &&sink, coroutine_t *co, std::function<void()> func);

  public:
    coroutine_t(std::function<void()> f)
        : context(std::bind(co_wrapper, std::placeholders::_1, this))
        , func(f)
        , prev(nullptr){};

    static coroutine_t *create(std::function<void()> f)
    {
        coroutine_t *co = new coroutine_t(f);
        return co;
    };

    static coroutine_t *current() { return co_cur; }

    static void remove(coroutine_t *c) { delete c; }

    // switch to this
    void resume()
    {
        if (co_cur)
        {
            prev = co_cur;
        }
        co_cur = this;
        context = std::move(context).resume();
    }

    static void yield()
    {
        coroutine_t *cur = current();
        if (cur)
        {
            co_cur = cur->prev;
            cur->context = std::move(cur->context).resume();
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
            cur->context =
                std::move(cur->context).resume_with(std::bind(co_reschedule_wrapper, std::placeholders::_1, cur, func));
        }
        else
        {
            throw std::exception();
        }
    }
};

template <typename Func, typename... Args> inline static auto await(Func func, Args &&... args)
{
    while (1)
    {
        auto ret = func(std::forward<Args>(args)...);
        if (ret.is_finish())
        {
            return ret();
        }
        coroutine_t::yield();
    }
}

} // namespace net::co