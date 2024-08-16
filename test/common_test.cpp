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
