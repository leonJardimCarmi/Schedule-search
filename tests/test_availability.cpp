#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "solver/Availability.h"

namespace {

// Builds test objects directly in code, without any JSON:
// the availability logic does not care where the data came from.
const Date kSunday = Date::fromString("2026-11-08");
const Date kMonday = Date::fromString("2026-11-09");

Shift makeShift(const Date& date, const std::string& time) {
    return Shift{"S", date, TimePeriod::fromString(time), 1, 1, ""};
}

AvailabilitySlot slot(const Date& date, const char* time, AvailabilityType type) {
    std::optional<TimePeriod> t;
    if (time) t = TimePeriod::fromString(time);
    return AvailabilitySlot{date, t, type};
}

Employee withSlots(std::initializer_list<AvailabilitySlot> slots) {
    Employee e;
    e.availability = slots;
    return e;
}

const AvailabilityType kAvail = AvailabilityType::Available;
const AvailabilityType kPref = AvailabilityType::Preferred;
const AvailabilityType kUnavail = AvailabilityType::Unavailable;

}  // namespace

TEST_CASE("no availability at all means No", "[Availability]") {
    CHECK(availabilityFit(Employee{}, makeShift(kSunday, "08:00-16:00")) == Fit::No);
}

TEST_CASE("the slot must cover the whole shift", "[Availability]") {
    Employee dana = withSlots({slot(kSunday, "09:00-17:00", kAvail)});
    CHECK(availabilityFit(dana, makeShift(kSunday, "09:00-17:00")) == Fit::Yes);
    CHECK(availabilityFit(dana, makeShift(kSunday, "10:00-14:00")) == Fit::Yes);
    CHECK(availabilityFit(dana, makeShift(kSunday, "08:00-16:00")) == Fit::No);  // starts 1h early
}

TEST_CASE("a slot on another day does not count", "[Availability]") {
    Employee e = withSlots({slot(kMonday, "08:00-16:00", kAvail)});
    CHECK(availabilityFit(e, makeShift(kSunday, "08:00-16:00")) == Fit::No);
}

TEST_CASE("preferred is reported as Preferred", "[Availability]") {
    Employee e = withSlots({slot(kSunday, "08:00-16:00", kPref)});
    CHECK(availabilityFit(e, makeShift(kSunday, "08:00-16:00")) == Fit::Preferred);
}

TEST_CASE("a slot without time covers the whole day", "[Availability]") {
    Employee e = withSlots({slot(kSunday, nullptr, kAvail)});
    CHECK(availabilityFit(e, makeShift(kSunday, "08:00-16:00")) == Fit::Yes);
    CHECK(availabilityFit(e, makeShift(kSunday, "22:00-06:00")) == Fit::Yes);  // starts that day
}

TEST_CASE("an overnight slot covers an overnight shift", "[Availability]") {
    Employee avi = withSlots({slot(kSunday, "22:00-06:00", kAvail)});
    CHECK(availabilityFit(avi, makeShift(kSunday, "22:00-06:00")) == Fit::Yes);
    CHECK(availabilityFit(avi, makeShift(kSunday, "16:00-00:00")) == Fit::No);
}

TEST_CASE("unavailable wins over available, in any order", "[Availability]") {
    const Shift morning = makeShift(kSunday, "08:00-16:00");

    Employee before = withSlots({slot(kSunday, "12:00-13:00", kUnavail),
                                 slot(kSunday, "08:00-16:00", kAvail)});
    Employee after = withSlots({slot(kSunday, "08:00-16:00", kAvail),
                                slot(kSunday, "12:00-13:00", kUnavail)});
    CHECK(availabilityFit(before, morning) == Fit::No);
    CHECK(availabilityFit(after, morning) == Fit::No);
}

TEST_CASE("unavailable that does not touch the shift changes nothing", "[Availability]") {
    Employee e = withSlots({slot(kSunday, "08:00-16:00", kAvail),
                            slot(kSunday, "18:00-20:00", kUnavail)});
    CHECK(availabilityFit(e, makeShift(kSunday, "08:00-16:00")) == Fit::Yes);
}

// ---- roleFits ----------------------------------------------------------------

TEST_CASE("roleFits: a shift without a role takes anyone", "[Availability][role]") {
    Employee guard;
    guard.roles = {"guard"};
    Employee nobody;   // no roles at all

    Shift anyJob = makeShift(kSunday, "08:00-16:00");
    Shift guardJob = makeShift(kSunday, "22:00-06:00");
    guardJob.role = "guard";

    CHECK(roleFits(guard, anyJob));
    CHECK(roleFits(nobody, anyJob));
    CHECK(roleFits(guard, guardJob));
    CHECK_FALSE(roleFits(nobody, guardJob));
}
