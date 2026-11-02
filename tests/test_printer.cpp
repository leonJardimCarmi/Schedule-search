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
