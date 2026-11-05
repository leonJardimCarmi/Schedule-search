#include <catch2/catch_test_macros.hpp>

#include <string>

#include "io/JsonLoader.h"

namespace {

// A small valid file; individual tests swap one piece of it.
std::string makeJson(const std::string& employees, const std::string& shifts,
                     const std::string& rules = "{}") {
    return R"({ "period": { "start": "2026-11-08", "end": "2026-11-14" },
                "employees": )" + employees + R"(,
                "shifts": )" + shifts + R"(,
                "rules": )" + rules + "}";
}

const std::string kOneEmployee = R"([
    { "id": 1, "name": "A", "availability": [
        { "date": "2026-11-08", "time": "08:00-16:00", "type": "available" } ] } ])";

const std::string kOneShift = R"([
    { "id": "S1", "date": "2026-11-08", "time": "08:00-16:00", "min": 1, "wanted": 1 } ])";

// True if any diagnostic's location contains `where` and its text contains `what`.
bool reported(const LoadResult& r, const std::string& where, const std::string& what) {
    for (const Diagnostic& d : r.diagnostics.all())
        if (d.where.find(where) != std::string::npos && d.message.find(what) != std::string::npos)
            return true;
    return false;
}

}  // namespace

TEST_CASE("a valid file loads without diagnostics", "[loader]") {
    LoadResult r = JsonLoader::loadString(makeJson(kOneEmployee, kOneShift), "t.json");
    REQUIRE(r.ok());
    REQUIRE(r.diagnostics.all().empty());
    REQUIRE(r.problem->employees.size() == 1);
    REQUIRE(r.problem->shifts.size() == 1);
    CHECK(r.problem->employees[0].availability[0].type == AvailabilityType::Available);
    CHECK(r.problem->rules.minRestHours == 10);                   // default (D15)
    CHECK(r.problem->rules.blockGapMinutes == 0);                 // default (D14)
    CHECK_FALSE(r.problem->rules.maxConsecutiveDays.has_value()); // not set = no limit
}

TEST_CASE("shifts are sorted chronologically, across midnight too", "[loader]") {
    const std::string shifts = R"([
        { "id": "N", "date": "2026-11-08", "time": "22:00-06:00", "min": 1, "wanted": 1 },
        { "id": "M2", "date": "2026-11-09", "time": "08:00-16:00", "min": 1, "wanted": 1 },
        { "id": "M1", "date": "2026-11-08", "time": "08:00-16:00", "min": 1, "wanted": 1 } ])";
    LoadResult r = JsonLoader::loadString(makeJson(kOneEmployee, shifts), "t.json");
    REQUIRE(r.ok());
    const auto& s = r.problem->shifts;
    CHECK(s[0].id == "M1");
    CHECK(s[1].id == "N");
    CHECK(s[2].id == "M2");
    // The night shift ends 2 hours before the next morning shift starts.
    CHECK(s[2].absoluteStart() - s[1].absoluteEnd() == 2 * 60);
}

TEST_CASE("syntax errors are reported, not thrown", "[loader]") {
    LoadResult r = JsonLoader::loadString("{ \"period\": ", "t.json");
    CHECK_FALSE(r.ok());
    CHECK(reported(r, "(syntax)", "parse error"));
}

TEST_CASE("errors carry the path to the bad field", "[loader]") {
    const std::string employees = R"([
        { "id": 1, "name": "A", "availability": [
            { "date": "2026-11-08", "time": "25:00-16:00", "type": "available" },
            { "date": "2026-11-09", "type": "maybe" } ] } ])";
    LoadResult r = JsonLoader::loadString(makeJson(employees, kOneShift), "t.json");
    CHECK_FALSE(r.ok());
    CHECK(reported(r, "t.json: employees[0].availability[0].time", "bad time"));
    CHECK(reported(r, "employees[0].availability[1].type", "must be available"));
}

TEST_CASE("all problems are reported in one run", "[loader]") {
    const std::string employees = R"([
        { "id": 1, "name": "A" },
        { "id": 1, "name": "B" },
        { "name": "no id" } ])";
    const std::string shifts = R"([
        { "id": "S1", "date": "2026-11-08", "time": "08:00-16:00", "min": 3, "wanted": 2 },
        { "id": "S2", "date": "2026-12-01", "time": "08:00-16:00", "min": 1, "wanted": 1 } ])";
    LoadResult r = JsonLoader::loadString(makeJson(employees, shifts), "t.json");
    CHECK_FALSE(r.ok());
    CHECK(reported(r, "employees[1]", "duplicate employee id"));
    CHECK(reported(r, "employees[2].id", "missing required field"));
    CHECK(reported(r, "shifts[S1]", "larger than wanted"));
    CHECK(reported(r, "shifts[S2].date", "outside the period"));
}

TEST_CASE("unknown fields are warnings, not errors", "[loader]") {
    LoadResult r = JsonLoader::loadString(
        makeJson(kOneEmployee, kOneShift, R"({ "minRestHour": 8 })"), "t.json");
    CHECK(r.ok());   // still loads
    CHECK(reported(r, "rules.minRestHour", "unknown field"));
}

TEST_CASE("a missing file is an error", "[loader]") {
    LoadResult r = JsonLoader::loadFile("no/such/file.json");
    CHECK_FALSE(r.ok());
    CHECK(reported(r, "no/such/file.json", "cannot open"));
}

TEST_CASE("the double flag is read from availability", "[loader][double]") {
    const std::string employees = R"([
        { "id": 1, "name": "A", "availability": [
            { "date": "2026-11-08", "time": "08:00-00:00", "type": "available", "double": true },
            { "date": "2026-11-09", "time": "08:00-16:00", "type": "available" },
            { "date": "2026-11-10", "type": "unavailable", "double": true },
            { "date": "2026-11-11", "time": "08:00-16:00", "type": "available", "double": "yes" } ] } ])";
    LoadResult r = JsonLoader::loadString(makeJson(employees, kOneShift), "t.json");

    CHECK(reported(r, "availability[3].double", "expected true or false"));         // error
    CHECK(reported(r, "availability[2].double", "no meaning on an unavailable"));   // warning
    REQUIRE(r.problem.has_value());
    const auto& slots = r.problem->employees[0].availability;
    CHECK(slots[0].allowsDouble);
    CHECK_FALSE(slots[1].allowsDouble);   // default
    CHECK_FALSE(slots[2].allowsDouble);   // ignored on unavailable
}
