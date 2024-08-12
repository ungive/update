#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ungive/update.hpp"

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
