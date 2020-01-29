#pragma once
#include <libcopp/coroutine/coroutine_context_container.h>

#include <list>

namespace net::co
{

template <typename T> class task
{
    bool ok;
    T val;

  public:
    T operator()() {}
};

class coroutine_t
{
  public:
};

class main_coroutine_t
{
    std::list<coroutine_t> cos, cos_ready;
    coroutine_t *current_co;

  public:
    template <typename RT, typename... Args> RT start_co() {}

    void resume() {}
    void yield() {}

    static main_coroutine_t &current() {}
};

void yield();
template <typename T> task<T> await();

#define AWAIT
#define YIELD
#define CRET

} // namespace net::co