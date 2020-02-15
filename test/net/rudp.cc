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

    rudp1.connect(addr2);
    rudp2.connect(addr1);

    int count_flag = 0;

    rudp1.run([&rudp1, &loop, &count_flag]() {
        socket_buffer_t buffer(test_data);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(rudp_awrite, &rudp1, buffer), io_result::ok);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(rudp_aread, &rudp1, buffer), io_result::ok);
        GTEST_ASSERT_EQ(buffer.to_string(), test_data);
        if (++count_flag > 1)
        {
            loop.exit(0);
        }
    });

    rudp2.run([&rudp2, &loop, &count_flag]() {
        socket_buffer_t buffer(test_data);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(rudp_awrite, &rudp2, buffer), io_result::ok);
        buffer.expect().origin_length();
        GTEST_ASSERT_EQ(co::await(rudp_aread, &rudp2, buffer), io_result::ok);
        GTEST_ASSERT_EQ(buffer.to_string(), test_data);
        if (++count_flag > 1)
        {
            loop.exit(0);
        }
    });
    // loop.add_timer(make_timer(net::make_timespan(1), [&loop]() { loop.exit(-1); }));
    loop.run();
    GTEST_ASSERT_EQ(count_flag, 2);
}