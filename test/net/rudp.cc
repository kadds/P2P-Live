#include "net/rudp.hpp"
#include "net/event.hpp"
#include "net/net.hpp"
#include <gtest/gtest.h>
using namespace net;
static std::string test_data = "12345678abcdefghe";

TEST(RUDPTest, Interface)
{
    event_context_t context(event_strategy::epoll);
    event_loop_t loop;
    context.add_event_loop(&loop);

    socket_addr_t addr1("127.0.0.1", 2001);
    socket_addr_t addr2("127.0.0.1", 2000);

    rudp_t rudp1, rudp2;
    rudp1.bind(context, addr1, true);
    rudp2.bind(context, addr2, true);

    rudp1.add_connection(addr2, 0, make_timespan(5));
    rudp2.add_connection(addr1, 0, make_timespan(5));

    int count_flag = 0;

    rudp1.on_new_connection([&rudp1, &loop, &count_flag](rudp_connection_t conn) {
        socket_buffer_t buffer(test_data);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(rudp_awrite, &rudp1, conn, buffer), io_result::ok);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(rudp_aread, &rudp1, conn, buffer), io_result::ok);

        GTEST_ASSERT_EQ(buffer.to_string(), test_data);
        if (++count_flag > 1)
        {
            loop.exit(0);
        }
    });

    rudp2.on_new_connection([&rudp2, &loop, &count_flag](rudp_connection_t conn) {
        socket_buffer_t buffer(test_data);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(rudp_awrite, &rudp2, conn, buffer), io_result::ok);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(rudp_aread, &rudp2, conn, buffer), io_result::ok);

        GTEST_ASSERT_EQ(buffer.to_string(), test_data);
        if (++count_flag > 1)
        {
            loop.exit(0);
        }
    });
    loop.add_timer(make_timer(net::make_timespan(1), [&loop]() { loop.exit(-1); }));
    loop.run();
    GTEST_ASSERT_EQ(count_flag, 2);
}

TEST(RUDPTest, FlowControl)
{
    constexpr u64 test_count = 50;

    event_context_t context(event_strategy::epoll);
    event_loop_t loop;
    context.add_event_loop(&loop);

    socket_addr_t addr1("127.0.0.1", 2002);
    socket_addr_t addr2("127.0.0.1", 2003);

    rudp_t rudp1, rudp2;
    rudp1.bind(context, addr1, true);
    rudp2.bind(context, addr2, true);

    rudp1.add_connection(addr2, 0, make_timespan(5));
    rudp2.add_connection(addr1, 0, make_timespan(5));
    rudp1.set_wndsize(addr2, 0, 5, 3);
    rudp2.set_wndsize(addr1, 0, 3, 5);

    int count_flag = 0;

    rudp1.on_new_connection([&rudp1, &loop, &count_flag](rudp_connection_t conn) {
        socket_addr_t addr;
        socket_buffer_t buffer(1280);
        buffer.clear();
        for (int i = 0; i < test_count; i++)
        {
            buffer.expect().origin_length();
            buffer.clear();
            GTEST_ASSERT_EQ(co::await(rudp_awrite, &rudp1, conn, buffer), io_result::ok);
        }

        if (++count_flag > 1)
        {
            loop.exit(0);
        }
    });

    rudp2.on_new_connection([&rudp2, &loop, &count_flag](rudp_connection_t conn) {
        socket_buffer_t buffer(1280);
        for (int i = 0; i < test_count; i++)
        {
            buffer.expect().origin_length();
            GTEST_ASSERT_EQ(co::await(rudp_aread, &rudp2, conn, buffer), io_result::ok);
            GTEST_ASSERT_EQ(buffer.get_length(), 1280);
        }

        if (++count_flag > 1)
        {
            loop.exit(0);
        }
    });
    loop.add_timer(make_timer(net::make_timespan(2), [&loop]() { loop.exit(-1); }));
    loop.run();
    GTEST_ASSERT_EQ(count_flag, 2);
}

static void thread_main(event_context_t *context, socket_addr_t addr1, socket_addr_t addr2, int test_count,
                        std::atomic_int &count_flag)
{
    event_loop_t loop;
    context->add_event_loop(&loop);

    rudp_t rudp2;
    rudp2.bind(*context, addr2, true);

    rudp2.add_connection(addr1, 0, make_timespan(5));
    rudp2.set_wndsize(addr1, 0, 3, 5);

    rudp2.on_new_connection([&rudp2, context, &count_flag, &test_count](rudp_connection_t conn) {
        socket_buffer_t buffer(1280);
        for (int i = 0; i < test_count; i++)
        {
            buffer.expect().origin_length();
            GTEST_ASSERT_EQ(co::await(rudp_aread, &rudp2, conn, buffer), io_result::ok);
            GTEST_ASSERT_EQ(buffer.get_length(), 1280);
        }

        if (++count_flag > 1)
        {
            context->exit_all_loop(0);
        }
    });

    loop.run();
    context->remove_event_loop(&loop);
}

TEST(RUDPTest, MultithreadFlowControl)
{
    constexpr u64 test_count = 50;

    event_context_t context(event_strategy::epoll);
    event_loop_t loop;
    context.add_event_loop(&loop);

    socket_addr_t addr1("127.0.0.1", 2004);
    socket_addr_t addr2("127.0.0.1", 2005);
    std::atomic_int count_flag = 0;

    std::thread thread(std::bind(&thread_main, &context, addr1, addr2, test_count, std::ref(count_flag)));

    rudp_t rudp1;
    rudp1.bind(context, addr1, true);

    rudp1.add_connection(addr2, 0, make_timespan(5));
    rudp1.set_wndsize(addr2, 0, 5, 3);

    rudp1.on_new_connection([&rudp1, &loop, &count_flag, &context](rudp_connection_t conn) {
        socket_addr_t addr;
        socket_buffer_t buffer(1280);
        buffer.clear();
        for (int i = 0; i < test_count; i++)
        {
            buffer.expect().origin_length();
            buffer.clear();
            GTEST_ASSERT_EQ(co::await(rudp_awrite, &rudp1, conn, buffer), io_result::ok);
        }

        if (++count_flag > 1)
        {
            context.exit_all_loop(0);
        }
    });

    loop.add_timer(make_timer(net::make_timespan(200), [&context]() { context.exit_all_loop(0); }));
    loop.run();
    GTEST_ASSERT_EQ(count_flag, 2);
    thread.join();
}
