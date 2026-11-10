#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>

#include "io/Printer.h"

namespace {

const Date kSunday = Date::fromString("2026-11-08");

Problem twoPeopleOneShift() {
    Employee a;
    a.id = 1;
    a.name = "Alice";
    a.availability.push_back({kSunday, std::nullopt, AvailabilityType::Available});
    Employee b;
    b.id = 2;
    b.name = "Bob";   // never available

    Shift s{"S1", kSunday, TimePeriod::fromString("08:00-16:00"), 1, 2, ""};
    return Problem{kSunday, kSunday, {a, b}, {s}, Rules{}};
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

}  // namespace

// The printer writes to any std::ostream. An ostringstream collects the
// text in memory, so the test can inspect exactly what would be printed.
TEST_CASE("printSchedule shows each shift with its staff", "[Printer]") {
    Problem p = twoPeopleOneShift();
    State schedule(2, 1);
    schedule.assign(0, 0);   // Alice -> S1

    std::ostringstream out;
    printSchedule(out, p, schedule);
    const std::string text = out.str();

    CHECK(contains(text, "Sun 2026-11-08"));
    CHECK(contains(text, "S1     08:00-16:00  1/2  Alice  (below wanted)"));
    CHECK(contains(text, "Alice            1 shifts   8 h   S1"));
    CHECK(contains(text, "Bob              0 shifts   0 h"));
}

TEST_CASE("printCandidates lists only employees who fit", "[Printer]") {
    Problem p = twoPeopleOneShift();
    Solver solver(p);

    std::ostringstream out;
    printCandidates(out, p, solver);

    CHECK(contains(out.str(), "S1       Alice"));
    CHECK_FALSE(contains(out.str(), "Bob"));
}

TEST_CASE("printSchedule flags employees below their minShifts", "[Printer][minShifts]") {
    Problem p = twoPeopleOneShift();
    p.employees[0].minShifts = 1;   // Alice wants at least 1: she gets it
    p.employees[1].minShifts = 2;   // Bob wants at least 2: he gets none

    State schedule(2, 1);
    schedule.assign(0, 0);

    std::ostringstream out;
    printSchedule(out, p, schedule);
    const std::string text = out.str();

    CHECK_FALSE(contains(text, "Alice            1 shifts   8 h   S1  (below"));
    CHECK(contains(text, "Bob              0 shifts   0 h    (below minimum 2)"));
}

TEST_CASE("printShortages explains why a shift cannot be staffed", "[Printer][D25]") {
    Problem p = twoPeopleOneShift();                 // Alice available Sunday, Bob never
    Shift night{"N", kSunday, TimePeriod::fromString("22:00-06:00"), 1, 1, "guard"};
    Shift big{"B", kSunday, TimePeriod::fromString("08:00-16:00"), 2, 2, ""};
    Shift empty{"X", kSunday + 1, TimePeriod::fromString("08:00-16:00"), 1, 1, ""};
    p.periodEnd = kSunday + 1;
    p.shifts = {big, night, empty};
    Solver solver(p);

    std::ostringstream out;
    printShortages(out, p, solver);
    const std::string text = out.str();

    CHECK(contains(text, "needs 2, can get 1: only 1 available"));
    CHECK(contains(text, "needs 1, can get 0: 1 available, but only 0 with role 'guard'"));
    CHECK(contains(text, "needs 1, can get 0: nobody is available at this time"));
}
