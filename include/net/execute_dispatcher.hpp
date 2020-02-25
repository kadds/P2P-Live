#pragma once
#include "lock.hpp"
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
    lock::spinlock_t lock;
    lock::spinlock_t cancel_lock;

  public:
    /// 非线程安全，必须由evenet loop 调用，执行队列中的execute context
    void dispatch();
    /// 线程安全函数。取消一个execute context
    void cancel(execute_context_t *econtext);
    /// 线程安全函数。将一个execute context添加到队列中，并设置唤醒是执行的函数
    void add(execute_context_t *econtext, std::function<void()> func);
};
} // namespace net
