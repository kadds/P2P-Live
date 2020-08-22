#include "net/co.hpp"

namespace net::co
{
ctx::fiber &&co_wrapper(ctx::fiber &&sink, coroutine_t *co)
{
    co->context = std::move(sink);
    try
    {
        co_cur->func();
    } catch (const coroutine_stop_exception &e)
    {
    }
    co->is_stop = true;
    return std::move(co->context);
}

ctx::fiber &&co_reschedule_wrapper(ctx::fiber &&sink, coroutine_t *co, std::function<void()> func)
{
    co->context = std::move(sink);
    func();
    return std::move(co->context);
}

}; // namespace net::co