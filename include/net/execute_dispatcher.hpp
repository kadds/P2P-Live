#pragma once
#include <functional>
#include <queue>
#include <tuple>
#include <unordered_set>

namespace net
{
class execute_context_t;
class execute_thread_dispatcher_t
{
    std::queue<std::tuple<execute_context_t *, std::function<void()>>> co_wait_for_resume;
    std::unordered_set<execute_context_t *> cancel_contexts;

  public:
    void dispatch();
    void cancel(execute_context_t *econtext);
    void add(execute_context_t *econtext, std::function<void()> func);
};
} // namespace net
