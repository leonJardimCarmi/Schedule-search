#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include "Date.h"

TEST_CASE("the epoch is day 0 and a Thursday", "[Date]") {
    Date epoch = Date::fromString("1970-01-01");
    REQUIRE(epoch.daysSinceEpoch() == 0);
    REQUIRE(epoch.weekday() == 4);
}

TEST_CASE("parse and print round-trip", "[Date]") {
    for (const char* text : {"2026-11-08", "2000-02-29", "1999-12-31", "2028-02-29"})
        CHECK(Date::fromString(text).toString() == text);
}

TEST_CASE("weekday of a known date", "[Date]") {
    CHECK(Date::fromString("2026-11-08").weekday() == 0);   // Sunday
    CHECK(Date::fromString("2026-11-14").weekday() == 6);   // Saturday
}

TEST_CASE("subtraction counts days across month and year ends", "[Date]") {
    CHECK(Date::fromString("2026-12-01") - Date::fromString("2026-11-30") == 1);
    CHECK(Date::fromString("2027-01-01") - Date::fromString("2026-12-31") == 1);
    CHECK(Date::fromString("2028-03-01") - Date::fromString("2028-02-28") == 2);  // leap year
    CHECK((Date::fromString("2026-12-31") + 1).toString() == "2027-01-01");
}

TEST_CASE("dates that do not exist are rejected", "[Date]") {
    CHECK_THROWS_AS(Date::fromString("2026-02-29"), std::invalid_argument);  // not a leap year
    CHECK_THROWS_AS(Date::fromString("2100-02-29"), std::invalid_argument);  // century rule
    CHECK_THROWS_AS(Date::fromString("2026-04-31"), std::invalid_argument);
    CHECK_THROWS_AS(Date::fromString("2026-13-01"), std::invalid_argument);
}

TEST_CASE("wrong format is rejected", "[Date]") {
    CHECK_THROWS_AS(Date::fromString("2026-1-8"), std::invalid_argument);
    CHECK_THROWS_AS(Date::fromString("08/11/2026"), std::invalid_argument);
    CHECK_THROWS_AS(Date::fromString(""), std::invalid_argument);
}
