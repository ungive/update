#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ungive/update/updater.hpp"

using namespace ungive::update;

TEST(version_number, ComparisonWorksWhenComparingTwoIdenticalLengthVersions)
{
    EXPECT_TRUE(version_number({ 1, 2, 2 }) < version_number({ 1, 2, 3 }));
    EXPECT_TRUE(version_number({ 1, 1, 3 }) < version_number({ 1, 2, 3 }));
    EXPECT_TRUE(version_number({ 0, 2, 3 }) < version_number({ 1, 2, 3 }));
    EXPECT_TRUE(version_number({ 1, 1, 1 }) < version_number({ 1, 2, 3 }));
    EXPECT_TRUE(version_number({ 0, 0, 0 }) < version_number({ 1, 2, 3 }));
    EXPECT_FALSE(version_number({ 1, 2, 2 }) > version_number({ 1, 2, 3 }));
    EXPECT_FALSE(version_number({ 1, 1, 3 }) > version_number({ 1, 2, 3 }));
    EXPECT_FALSE(version_number({ 0, 2, 3 }) > version_number({ 1, 2, 3 }));
    EXPECT_FALSE(version_number({ 1, 1, 1 }) > version_number({ 1, 2, 3 }));
    EXPECT_FALSE(version_number({ 0, 0, 0 }) > version_number({ 1, 2, 3 }));
    EXPECT_TRUE(version_number({ 1, 2, 3 }) == version_number({ 1, 2, 3 }));
    EXPECT_TRUE(version_number({ 1, 2, 2 }) != version_number({ 1, 2, 3 }));
    EXPECT_TRUE(version_number({ 1, 2, 3 }) >= version_number({ 1, 2, 3 }));
    EXPECT_TRUE(version_number({ 1, 2, 4 }) >= version_number({ 1, 2, 3 }));
}

TEST(version_number, ComparisonWorksWhenComparingTwoDifferentLengthVersions)
{
    EXPECT_TRUE(version_number({ 1, 1, 2 }) < version_number({ 1, 2 }));
    EXPECT_TRUE(version_number({ 1, 2, 0 }) == version_number({ 1, 2 }));
    EXPECT_TRUE(version_number({ 1, 2, 1 }) > version_number({ 1, 2 }));
    EXPECT_TRUE(version_number({ 1, 2 }) > version_number({ 1, 1, 2 }));
    EXPECT_TRUE(version_number({ 1, 2 }) == version_number({ 1, 2, 0 }));
    EXPECT_TRUE(version_number({ 1, 2 }) < version_number({ 1, 2, 1 }));
}

TEST(version_number, InitializationWorksWithVarArgs)
{
    EXPECT_TRUE(version_number(1, 1, 2) < version_number(1, 2, 3));
}

TEST(version_number, InitializationWorksWithString)
{
    using v = version_number;
    EXPECT_EQ(v(1, 1, 2), v::from_string("1.1.2"));
    EXPECT_EQ(v(1, 2, 3), v::from_string("helloworld1.2.3", "helloworld"));
    EXPECT_EQ(v(1, 1, 2, 4), v::from_string("1.1.2.4"));
    EXPECT_EQ(v(101, 133, 214, 41111), v::from_string("101.133.214.41111"));
    EXPECT_EQ(v(1), v::from_string("1"));
    EXPECT_EQ(v(1), v::from_string("v1", "v"));
    EXPECT_EQ(v(23), v::from_string("v23", "v"));
    EXPECT_ANY_THROW(v::from_string("1.1.2.4x"));
    EXPECT_ANY_THROW(v::from_string("1.1.2", "x"));
    EXPECT_ANY_THROW(v::from_string("x1.x1.2.4", "x"));
}

TEST(parse_sha256sums, ParsesSha256Sums)
{
    const char* sums = R"(
b76b0958634d3f0e0140f294373615d88f1d36ed1f65c720605b6ca9d0160f11 *music-presence-2.2.2-mac-arm64.dmg
c4aaf74e72909e677dcbbbab47c867a16a921d5699d69ffdd50df9fa672b2c7b *music-presence-2.2.2-mac-x64.dmg
d8d107ddf60cae12558972c064bebd97266af02425553d65a10f3f72b8a6f0ed *music-presence-2.2.2-win64.exe
fef29d7f40be8d62c6ea654404275e7d4776f055edc7b18e65923fbbab53cfaa *music-presence-2.2.2-win64.zip
)";
    auto res = internal::crypto::parse_sha256sums(sums);
    EXPECT_EQ(
        "b76b0958634d3f0e0140f294373615d88f1d36ed1f65c720605b6ca9d0160f11",
        res[0].first);
    EXPECT_EQ(
        "c4aaf74e72909e677dcbbbab47c867a16a921d5699d69ffdd50df9fa672b2c7b",
        res[1].first);
    EXPECT_EQ(
        "d8d107ddf60cae12558972c064bebd97266af02425553d65a10f3f72b8a6f0ed",
        res[2].first);
    EXPECT_EQ(
        "fef29d7f40be8d62c6ea654404275e7d4776f055edc7b18e65923fbbab53cfaa",
        res[3].first);
    EXPECT_EQ("music-presence-2.2.2-mac-arm64.dmg", res[0].second);
    EXPECT_EQ("music-presence-2.2.2-mac-x64.dmg", res[1].second);
    EXPECT_EQ("music-presence-2.2.2-win64.exe", res[2].second);
    EXPECT_EQ("music-presence-2.2.2-win64.zip", res[3].second);
}
