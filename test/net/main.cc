#include "net/net.hpp"
#include <gtest/gtest.h>
int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    net::init_lib();

    int v = RUN_ALL_TESTS();
    net::uninit_lib();
    return v;
}