#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "io/ScheduleFile.h"

// Stage 7.1 (D51): the schedule file, written by --save and read by --check.

namespace {

const Date kSunday = Date::fromString("2026-11-08");

Employee person(int id, const std::string& name) {
    Employee e;
    e.id = id;
    e.name = name;
    return e;
}

// Three employees, three shifts. No availability: the file does not check it.
Problem smallProblem() {
    return Problem{kSunday, kSunday,
                   {person(101, "Yossi Cohen"), person(102, "Dana Levi"), person(103, "Avi Mizrahi")},
                   {Shift{"SUN-M", kSunday, TimePeriod::fromString("08:00-16:00"), 1, 2, ""},
                    Shift{"SUN-E", kSunday, TimePeriod::fromString("16:00-00:00"), 1, 1, ""},
                    Shift{"SUN-N", kSunday, TimePeriod::fromString("22:00-06:00"), 1, 1, ""}},
                   Rules{}};
}

bool reported(const Diagnostics& d, Severity severity, const std::string& where, const std::string& what) {
    for (const Diagnostic& x : d.all())
        if (x.severity == severity && x.where.find(where) != std::string::npos &&
            x.message.find(what) != std::string::npos)
            return true;
    return false;
}

}  // namespace

TEST_CASE("a schedule is written one shift per line, with employee ids", "[ScheduleFile][D51]") {
    const Problem p = smallProblem();
    State s(3, 3);
    s.assign(0, 0);
    s.assign(1, 0);
    s.assign(2, 2);
    CHECK(scheduleToJson(p, s) ==
          "{\n"
          "  \"assignments\": {\n"
          "    \"SUN-M\": [101, 102],\n"
          "    \"SUN-E\": [],\n"
          "    \"SUN-N\": [103]\n"
          "  }\n"
          "}\n");
}

TEST_CASE("saving and loading gives back the same schedule", "[ScheduleFile][D51]") {
    const Problem p = smallProblem();
    State s(3, 3);
    s.assign(1, 0);
    s.assign(0, 0);
    s.assign(1, 1);
    const ScheduleLoadResult back = loadScheduleString(scheduleToJson(p, s), "t.json", p);
    REQUIRE(back.ok());
    CHECK(back.diagnostics.all().empty());
    CHECK(differentAssignments(*back.schedule, s) == 0);
}

TEST_CASE("employees may be given by id or by full name", "[ScheduleFile][D51]") {
    const Problem p = smallProblem();
    const ScheduleLoadResult r = loadScheduleString(
        R"({"assignments": {"SUN-M": [101, "dana levi"], "SUN-E": ["103"], "SUN-N": []}})", "t.json", p);
    REQUIRE(r.ok());
    CHECK(r.schedule->staffOf(0) == std::vector<int>{0, 1});
    CHECK(r.schedule->staffOf(1) == std::vector<int>{2});
    CHECK(r.schedule->staffOf(2).empty());
}

TEST_CASE("a shift missing from the file is empty, with a warning", "[ScheduleFile][D51]") {
    const Problem p = smallProblem();
    const ScheduleLoadResult r = loadScheduleString(R"({"assignments": {"SUN-M": [101]}})", "t.json", p);
    REQUIRE(r.ok());
    CHECK(r.schedule->staffOf(1).empty());
    CHECK(reported(r.diagnostics, Severity::Warning, "assignments", "2 shift(s) not in the file, taken as empty: SUN-E, SUN-N"));
}

TEST_CASE("mistakes in the schedule file are errors with their place", "[ScheduleFile][D51]") {
    const Problem p = smallProblem();
    auto load = [&](const std::string& text) { return loadScheduleString(text, "t.json", p); };

    SECTION("not JSON") { CHECK_FALSE(load("{oops").ok()); }
    SECTION("no assignments") {
        const auto r = load(R"({"shifts": {}})");
        CHECK_FALSE(r.ok());
        CHECK(reported(r.diagnostics, Severity::Error, "assignments", "missing"));
        CHECK(reported(r.diagnostics, Severity::Warning, "shifts", "unknown field"));
    }
    SECTION("unknown shift") {
        const auto r = load(R"({"assignments": {"MON-M": [101]}})");
        CHECK_FALSE(r.ok());
        CHECK(reported(r.diagnostics, Severity::Error, "assignments.MON-M", "no shift"));
    }
    SECTION("unknown employee") {
        const auto r = load(R"({"assignments": {"SUN-M": [101, "Noa"], "SUN-E": [999]}})");
        CHECK_FALSE(r.ok());
        CHECK(reported(r.diagnostics, Severity::Error, "assignments.SUN-M[1]", "no employee \"Noa\""));
        CHECK(reported(r.diagnostics, Severity::Error, "assignments.SUN-E[0]", "no employee 999"));
    }
    SECTION("the same person twice in one shift") {
        const auto r = load(R"({"assignments": {"SUN-M": [101, "Yossi Cohen"]}})");
        CHECK_FALSE(r.ok());
        CHECK(reported(r.diagnostics, Severity::Error, "assignments.SUN-M[1]", "Yossi Cohen is listed twice"));
    }
    SECTION("wrong types") {
        CHECK_FALSE(load(R"({"assignments": {"SUN-M": 101}})").ok());
        CHECK_FALSE(load(R"({"assignments": {"SUN-M": [true]}})").ok());
    }
}
