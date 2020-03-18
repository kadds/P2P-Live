#include "net/endian.hpp"
#include <functional>
#include <gtest/gtest.h>
using namespace net;

#pragma pack(push, 1)
struct data2_t
{
    u8 tag;       // 1
    u16 tag2;     // 2
    u32 version;  // 4
    u64 version2; // 8
    using member_list_t = net::serialization::typelist_t<u8, u16, u32, u64>;
};

struct data_t
{
    u16 head;       // 2
    data2_t header; //
                    // char data[24];       // 24
    i16 tail;       // 2
    u8 data[4];
    i16 data2[3];
    using member_list_t = net::serialization::typelist_t<u16, data2_t, i16, u8[4], i16[3]>;
};
#pragma pack(pop)

TEST(EndianTest, Test)
{
    data_t data;

    data2_t data2;
    data2.tag = 1;
    data2.tag2 = 384;
    data2.version = 2048;
    data2.version2 = 0x8040201008040201ULL;
    memcpy(&data.header, &data2, sizeof(data2));
    net::endian::cast(data2);

    GTEST_ASSERT_EQ(data2.tag, 1);
    GTEST_ASSERT_EQ(data2.tag2, 32769);
    GTEST_ASSERT_EQ(data2.version, 524288);
    GTEST_ASSERT_EQ(data2.version2, 0x102040810204080ULL);
    /// nest struct
    data.head = 2064;
    data.tail = -8000;
    data.data[0] = 1;
    data.data[1] = 2;
    data.data[2] = 0;
    data.data[3] = 4;

    data.data2[0] = 123;
    data.data2[1] = -8000;
    data.data2[2] = 0;

    net::endian::cast(data);
    GTEST_ASSERT_EQ(data.header.tag, 1);
    GTEST_ASSERT_EQ(data.header.tag2, 32769);
    GTEST_ASSERT_EQ(data.header.version, 524288);
    GTEST_ASSERT_EQ(data.header.version2, 0x102040810204080ULL);
    GTEST_ASSERT_EQ(data.head, 4104);
    GTEST_ASSERT_EQ(data.tail, -16160);
    GTEST_ASSERT_EQ(data.data[0], 1);
    GTEST_ASSERT_EQ(data.data[1], 2);
    GTEST_ASSERT_EQ(data.data[2], 0);
    GTEST_ASSERT_EQ(data.data[3], 4);
    GTEST_ASSERT_EQ(data.data2[0], 31488);
    GTEST_ASSERT_EQ(data.data2[1], -16160);
    GTEST_ASSERT_EQ(data.data2[2], 0);
}
