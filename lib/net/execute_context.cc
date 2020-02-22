#include "net/execute_context.hpp"
#include "net/co.hpp"
#include "net/event.hpp"
#include "net/execute_dispatcher.hpp"
namespace net
{

microsecond_t execute_context_t::sleep(microsecond_t ms)
{
    auto cur = get_current_time();
    stop_for(ms);
    auto now = get_current_time();
    if (now - cur >= ms)
        return 0;
    return now - cur;
}

void execute_context_t::stop() { co::coroutine_t::yield(); }

void execute_context_t::stop_for(microsecond_t ms, std::function<void()> func)
{
    if (timer.id >= 0)
        loop->remove_timer(timer);
    timer = loop->add_timer(make_timer(ms, [this, func]() {
        timer.id = -1;
        start();
    }));

    co::coroutine_t::yield();
    loop->remove_timer(timer);
    func();
}

void execute_context_t::stop_for(microsecond_t ms)
{
    if (timer.id >= 0)
        loop->remove_timer(timer);
    timer = loop->add_timer(make_timer(ms, [this]() {
        timer.id = -1;
        start();
    }));
    co::coroutine_t::yield();
    loop->remove_timer(timer);
}

void execute_context_t::start() { loop->get_dispatcher().add(this, std::function<void()>()); }

void execute_context_t::start_with(std::function<void()> func) { loop->get_dispatcher().add(this, func); }

void execute_context_t::run(std::function<void()> func)
{
    co = co::coroutine_t::create(func);
    co->set_execute_context(this);
    start();
}

void execute_context_t::wake_up_thread() { loop->wake_up(); }

execute_context_t::execute_context_t()
    : co(nullptr)
{
    timer.id = -1;
}

execute_context_t::~execute_context_t()
{
    if (co)
    {
        co->set_execute_context(nullptr);
        co->stop();
        loop->get_dispatcher().cancel(this);
    }
}

} // namespace net
