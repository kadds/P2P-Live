#include "net/co.hpp"
#include <sys/mman.h>

namespace net::co
{
ctx::fiber &&co_wrapper(ctx::fiber &&sink, coroutine_t *co)
{
    co->context = std::move(sink);
    co_cur->func();
    /// FIXME: return co
    std::move(co->context).resume();
}

ctx::fiber &&co_reschedule_wrapper(ctx::fiber &&sink, coroutine_t *co, std::function<void()> func)
{
    co->context = std::move(sink);
    func();
    return std::move(co->context);
}

}; // namespace net::co