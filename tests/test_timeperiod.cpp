#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include "TimePeriod.h"

TEST_CASE("fromString parses a regular day range", "[TimePeriod]") {
    TimePeriod p = TimePeriod::fromString("08:00-16:00");
    REQUIRE(p.getStart() == 8 * 60);
    REQUIRE(p.getEnd() == 16 * 60);
    REQUIRE(p.duration() == 8 * 60);
    REQUIRE(p.toString() == "08:00-16:00");
}

TEST_CASE("a range that ends before it starts crosses midnight", "[TimePeriod]") {
    TimePeriod night = TimePeriod::fromString("22:00-06:00");
    REQUIRE(night.getStart() == 22 * 60);
    REQUIRE(night.getEnd() == 24 * 60 + 6 * 60);   // stored as next-day minutes
    REQUIRE(night.duration() == 8 * 60);
    REQUIRE(night.toString() == "22:00-06:00");     // printed back the same way
}

TEST_CASE("contains: availability must cover the whole shift", "[TimePeriod]") {
    TimePeriod available = TimePeriod::fromString("08:00-16:00");

    CHECK(available.contains(TimePeriod::fromString("08:00-16:00")));  // exact fit
    CHECK(available.contains(TimePeriod::fromString("09:00-12:00")));  // inside
    CHECK_FALSE(available.contains(TimePeriod::fromString("07:00-12:00")));  // starts early
    CHECK_FALSE(available.contains(TimePeriod::fromString("12:00-17:00")));  // ends late
}

TEST_CASE("overlaps: touching ends do not overlap", "[TimePeriod]") {
    TimePeriod morning = TimePeriod::fromString("08:00-16:00");

    CHECK(morning.overlaps(TimePeriod::fromString("15:00-23:00")));
    CHECK_FALSE(morning.overlaps(TimePeriod::fromString("16:00-23:00")));  // back-to-back
    CHECK_FALSE(morning.overlaps(TimePeriod::fromString("00:00-08:00")));
}

TEST_CASE("bad input throws instead of producing garbage", "[TimePeriod]") {
    CHECK_THROWS_AS(TimePeriod::fromString("0800-1600"), std::invalid_argument);
    CHECK_THROWS_AS(TimePeriod::fromString("25:00-26:00"), std::invalid_argument);
    CHECK_THROWS_AS(TimePeriod::fromString("08:61-09:00"), std::invalid_argument);
}
