#pragma once
#include "event.hpp"
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

thread_local inline coroutine_t *co_cur = nullptr;

constexpr int stack_size = 1 << 16;

ctx::fiber &&co_wrapper(ctx::fiber &&sink, coroutine_t *co);
ctx::fiber &&co_reschedule_wrapper(ctx::fiber &&sink, coroutine_t *co, std::function<void()> func);

/// coroutine
class coroutine_t
{
    ctx::fiber context;
    std::function<void()> func;
    /// previous coroutine
    /// make up a list chain
    coroutine_t *prev;
    friend ctx::fiber &&co_wrapper(ctx::fiber &&sink, coroutine_t *co);
    friend ctx::fiber &&co_reschedule_wrapper(ctx::fiber &&sink, coroutine_t *co, std::function<void()> func);

  public:
    coroutine_t(std::function<void()> f)
        : context(std::bind(co_wrapper, std::placeholders::_1, this))
        , func(f)
        , prev(nullptr){};

    coroutine_t(const coroutine_t &) = delete;
    coroutine_t &operator=(const coroutine_t &) = delete;

    static coroutine_t *create(std::function<void()> f)
    {
        coroutine_t *co = new coroutine_t(f);
        return co;
    };

    static coroutine_t *current() { return co_cur; }

    static bool in_main_coroutine() { return co_cur == nullptr; }

    static bool in_coroutine(coroutine_t *co) { return co_cur == co; }

    static void remove(coroutine_t *c) { delete c; }

    // switch to this
    void resume()
    {
        prev = co_cur;

        co_cur = this;
        context = std::move(context).resume();
    }

    // switch to this and call func
    void resume_with(std::function<void()> func)
    {
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
            cur->context = std::move(cur->context).resume();
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
            cur->context =
                std::move(cur->context).resume_with(std::bind(co_reschedule_wrapper, std::placeholders::_1, cur, func));
            return;
        }
        else
        {
            throw std::exception();
        }
    }
};

class paramter_t
{
    /// how many times called
    int times;
    //. stop right now because timeout
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
    timer_registered_t reg_timer;
    while (1)
    {
        auto ret = func(param, std::forward<Args>(args)...);
        if (ret.is_finish())
        {
            if (param.get_times() != 0)
            {
                event_loop_t::current().remove_timer(reg_timer);
            }
            return ret();
        }
        if (param.get_times() == 0)
        {
            auto timer = make_timer(span, [co, &param]() {
                param.stop_wait();
                /// XXX: coroutine may destroy by main thread
                co->resume();
            });
            reg_timer = event_loop_t::current().add_timer(timer);
        }
        param.add_times();
        coroutine_t::yield();
    }
}

} // namespace net::co