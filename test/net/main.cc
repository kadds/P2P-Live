#include "net/net.hpp"
#include <gtest/gtest.h>
int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    net::init_lib();

    int v = 0;
    try
    {
        v = RUN_ALL_TESTS();
        net::uninit_lib();
    } catch (std::exception e)
    {
        std::cout << e.what() << std::endl;
        return v;
    }
    return v;
}