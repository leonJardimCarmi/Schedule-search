#include <catch2/catch_test_macros.hpp>

#include <string>

#include "io/CsvLoader.h"

namespace {

const Date kSunday = Date::fromString("2026-11-08");

// Two employees with no availability yet; the CSV fills it in.
Problem twoEmployees() {
    Employee a;
    a.id = 101;
    a.name = "Yossi";
    Employee b;
    b.id = 102;
    b.name = "Dana";
    return Problem{kSunday, kSunday + 6, {a, b}, {}, Rules{}};
}

bool reported(const Diagnostics& d, const std::string& where, const std::string& what) {
    for (const Diagnostic& x : d.all())
        if (x.where.find(where) != std::string::npos && x.message.find(what) != std::string::npos)
            return true;
    return false;
}

}  // namespace

TEST_CASE("csv: valid lines are added to the right employee", "[csv]") {
    Problem p = twoEmployees();
    Diagnostics d;
    CsvLoader::loadAvailabilityString(
        "employee_id,date,time,type,double\n"
        "# a comment\n"
        "\n"
        "101,2026-11-08,08:00-16:00,available\n"
        "102,2026-11-10,,unavailable\n"
        "102,2026-11-11,08:00-00:00,available,yes\n",
        "a.csv", p, d);

    REQUIRE(d.all().empty());
    REQUIRE(p.employees[0].availability.size() == 1);
    REQUIRE(p.employees[1].availability.size() == 2);

    const AvailabilitySlot& yossi = p.employees[0].availability[0];
    CHECK(yossi.date == kSunday);
    CHECK(yossi.time->toString() == "08:00-16:00");
    CHECK(yossi.type == AvailabilityType::Available);
    CHECK_FALSE(yossi.allowsDouble);   // 4 columns: no double column

    const AvailabilitySlot& wholeDay = p.employees[1].availability[0];
    CHECK_FALSE(wholeDay.time.has_value());   // empty time = whole day
    CHECK(wholeDay.type == AvailabilityType::Unavailable);

    CHECK(p.employees[1].availability[1].allowsDouble);   // "yes"
}

TEST_CASE("csv: files saved from Excel (BOM and \\r\\n) load the same", "[csv]") {
    Problem p = twoEmployees();
    Diagnostics d;
    CsvLoader::loadAvailabilityString(
        "\xEF\xBB\xBF" "employee_id,date,time,type\r\n"
        "101,2026-11-08,08:00-16:00,preferred\r\n",
        "excel.csv", p, d);

    REQUIRE(d.all().empty());
    REQUIRE(p.employees[0].availability.size() == 1);
    CHECK(p.employees[0].availability[0].type == AvailabilityType::Preferred);
}

TEST_CASE("csv: every bad line is reported with its line number", "[csv]") {
    Problem p = twoEmployees();
    Diagnostics d;
    CsvLoader::loadAvailabilityString(
        "employee_id,date,time,type,double\n"     // 1
        "999,2026-11-08,08:00-16:00,available\n"  // 2 unknown employee
        "abc,2026-11-08,08:00-16:00,available\n"  // 3 not a number
        "101,2026-02-30,08:00-16:00,available\n"  // 4 no such date
        "101,2026-11-08,25:00-16:00,available\n"  // 5 bad time
        "101,2026-11-08,08:00-16:00,maybe\n"      // 6 bad type
        "101,2026-11-08,08:00-16:00,available,sure\n"  // 7 bad double
        "101,2026-11-08\n"                         // 8 too few fields
        "102,2026-11-09,08:00-16:00,available\n",  // 9 fine
        "a.csv", p, d);

    CHECK(reported(d, "a.csv:2", "no employee with id 999"));
    CHECK(reported(d, "a.csv:3", "not a number"));
    CHECK(reported(d, "a.csv:4", "does not exist"));
    CHECK(reported(d, "a.csv:5", "bad time"));
    CHECK(reported(d, "a.csv:6", "type must be"));
    CHECK(reported(d, "a.csv:7", "double must be"));
    CHECK(reported(d, "a.csv:8", "expected 4 or 5 fields"));
    CHECK(d.hasErrors());

    // Bad lines are skipped, the good one still loads.
    CHECK(p.employees[0].availability.empty());
    CHECK(p.employees[1].availability.size() == 1);
}

TEST_CASE("csv: dates outside the period are warnings", "[csv]") {
    Problem p = twoEmployees();
    Diagnostics d;
    CsvLoader::loadAvailabilityString("101,2026-12-01,08:00-16:00,available\n", "a.csv", p, d);
    CHECK_FALSE(d.hasErrors());
    CHECK(reported(d, "a.csv:1", "outside the period"));
}

TEST_CASE("csv: a missing file is an error", "[csv]") {
    Problem p = twoEmployees();
    Diagnostics d;
    CsvLoader::loadAvailability("no/such/file.csv", p, d);
    CHECK(reported(d, "no/such/file.csv", "cannot open"));
}
