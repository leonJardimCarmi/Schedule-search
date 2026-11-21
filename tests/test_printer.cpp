#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>

#include "io/Printer.h"
#include "io/TracePrinter.h"

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

// The printer shows a Report (D41); build one the way main does.
Report reportOf(const Problem& p, const State& schedule, const std::vector<Shortage>& shortages = {}) {
    return buildReport(p, schedule, shortages, makeScoreModel(p), {});
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
    printSchedule(out, p, reportOf(p, schedule));
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
    printSchedule(out, p, reportOf(p, schedule));
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
    State nobody(2, 3);

    std::ostringstream out;
    printShortages(out, p, reportOf(p, nobody, solver.shortages()));
    const std::string text = out.str();

    CHECK(contains(text, "needs 2, can get 1: only 1 available"));
    CHECK(contains(text, "needs 1, can get 0: 1 available, but only 0 with role 'guard'"));
    CHECK(contains(text, "needs 1, can get 0: nobody is available at this time"));
}

TEST_CASE("per-employee shifts are listed in calendar order even when assigned out of order", "[Printer][mrv]") {
    Problem p = twoPeopleOneShift();
    Shift later{"S2", kSunday, TimePeriod::fromString("18:00-22:00"), 1, 1, ""};
    p.shifts.push_back(later);                       // index 1 = later in the day
    State schedule(2, 2);
    schedule.assign(0, 1);                           // MRV may fill the later shift first
    schedule.assign(0, 0);

    std::ostringstream out;
    printSchedule(out, p, reportOf(p, schedule));
    CHECK(contains(out.str(), "S1 S2"));
    CHECK_FALSE(contains(out.str(), "S2 S1"));
}

// ---- the weekly table (step 6.3, D42) -----------------------------------------

TEST_CASE("shortNames adds an initial only when first names repeat", "[Printer][D42]") {
    Problem p{kSunday, kSunday, {}, {}, Rules{}};
    for (const char* name : {"Noa Azoulay", "Noa Levi", "Omer Cohen", "Avi Cohen", "Avi Carmi", "Madonna"}) {
        Employee e;
        e.name = name;
        p.employees.push_back(e);
    }
    CHECK(shortNames(p) == std::vector<std::string>{"Noa A.", "Noa L.", "Omer", "Avi Cohen", "Avi Carmi", "Madonna"});
}

TEST_CASE("printWeeks shows a table with marks and missing people", "[Printer][D42]") {
    Problem p = twoPeopleOneShift();       // S1 on Sunday: wants 2, Alice can work it
    State schedule(2, 1);
    schedule.assign(0, 0);

    std::ostringstream out;
    printWeeks(out, p, reportOf(p, schedule), std::nullopt);
    const std::string text = out.str();

    CHECK(contains(text, "Week 1  2026-11-08 .. 2026-11-14"));
    CHECK(contains(text, "Sun 08/11"));
    CHECK(contains(text, "Sat 14/11"));
    CHECK(contains(text, "08:00-16:00  Alice"));
    CHECK(contains(text, "! -1"));            // one person below wanted
    CHECK(contains(text, "Week 1: 1 shifts, 0 full, 1 below wanted"));
    CHECK_FALSE(contains(text, " \n"));       // no trailing spaces
}

// ---- colors (step 6.3b, D45) ---------------------------------------------------

namespace {

// Removes ANSI color codes: ESC [ ... m
std::string withoutColors(const std::string& text) {
    std::string plain;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\033') {
            while (i < text.size() && text[i] != 'm') ++i;
            continue;
        }
        plain += text[i];
    }
    return plain;
}

}  // namespace

TEST_CASE("colors mark the cells and never shift the columns", "[Printer][D45]") {
    Problem p = twoPeopleOneShift();                              // S1: wants 2
    p.employees[0].availability[0].type = AvailabilityType::Preferred;
    State schedule(2, 1);
    schedule.assign(0, 0);                                        // Alice, preferred; one missing
    const Report report = reportOf(p, schedule);

    std::ostringstream plain, colored;
    printWeeks(plain, p, report, std::nullopt);                   // colors are off by default
    printWeeks(colored, p, report, std::nullopt, true);

    CHECK(plain.str().find('\033') == std::string::npos);
    CHECK(contains(colored.str(), "\033[32mAlice*\033[0m"));     // green: preferred
    CHECK(contains(colored.str(), "\033[33m! -1\033[0m"));       // yellow: below wanted
    // Same text and the same column positions once the codes are removed.
    CHECK(withoutColors(colored.str()) == plain.str());
}

// ---- --trace (step 6.5b, D48) -------------------------------------------------

namespace {

// Alice and Bob, both free on Sunday; two shifts at the same time, one
// person each. Whoever takes A is blocked for B (no-overlap).
Problem twoShiftsAtOnce() {
    Problem p = twoPeopleOneShift();
    p.employees[1].availability.push_back({kSunday, std::nullopt, AvailabilityType::Available});
    p.shifts = {Shift{"A", kSunday, TimePeriod::fromString("08:00-16:00"), 1, 1, ""},
                Shift{"B", kSunday, TimePeriod::fromString("08:00-16:00"), 1, 1, ""}};
    return p;
}

}  // namespace

TEST_CASE("the trace shows the whole search, indented by depth", "[Printer][D48]") {
    const Problem p = twoShiftsAtOnce();
    Solver solver(p);
    std::ostringstream out;
    TracePrinter trace(out, p, solver.constraints(), 100);
    solver.setObserver(&trace);

    REQUIRE(solver.countAll() == 2);
    trace.finish();
    CHECK(out.str() ==
          "--- trace: one level of indentation per shift already decided ---\n"
          "A: 2 allowed, min 1, slack 1 (the least of 2 open)\n"
          "  A: try 1 person\n"
          "  + Alice\n"
          "  B: 1 allowed, min 1, slack 0 (the last one open); blocked: Alice (no-overlap)\n"
          "    B: try 1 person\n"
          "    + Bob\n"
          "    schedule #1\n"
          "    - Bob\n"
          "  - Alice\n"
          "  + Bob\n"
          "  B: 1 allowed, min 1, slack 0 (the last one open); blocked: Bob (no-overlap)\n"
          "    B: try 1 person\n"
          "    + Alice\n"
          "    schedule #2\n"
          "    - Alice\n"
          "  - Bob\n"
          "--- end of trace: the whole search, 16 lines ---\n\n");
}

TEST_CASE("the trace stops printing at its limit, the search does not", "[Printer][D48]") {
    const Problem p = twoShiftsAtOnce();
    Solver solver(p);
    std::ostringstream out;
    TracePrinter trace(out, p, solver.constraints(), 3);
    solver.setObserver(&trace);

    CHECK(solver.countAll() == 2);   // the same answer as without a trace
    trace.finish();
    CHECK(trace.linesPrinted() == 3);
    CHECK(trace.stepsLeftOut() == 13);
    CHECK(out.str() ==
          "--- trace: one level of indentation per shift already decided ---\n"
          "A: 2 allowed, min 1, slack 1 (the least of 2 open)\n"
          "  A: try 1 person\n"
          "  + Alice\n"
          "--- trace stopped after 3 lines; the search went on for 13 more steps (--trace-lines <n> shows more) ---\n\n");
}

TEST_CASE("the trace shows scores, prunes and dead ends", "[Printer][D48]") {
    const Problem p = twoShiftsAtOnce();
    Solver solver(p);
    std::ostringstream out;
    TracePrinter trace(out, p, solver.constraints(), 100);

    trace.scheduleFound(2, nullptr, -1);
    const Score s{{0, 7}};
    trace.scheduleFound(2, &s, 0);
    trace.scheduleFound(2, &s, 2);
    trace.scheduleFound(2, &s, -1);
    trace.pruned(1, Score{{0, 9}}, s);
    State state(2, 2);
    state.assign(0, 0);
    state.assign(1, 0);
    const std::vector<int> both{0, 1};
    trace.deadEnd(state, 1, SearchObserver::ShiftChoice{1, 0, 1, -1, 1, &both});

    const std::string text = out.str();
    CHECK(contains(text, "    schedule #1\n"));
    CHECK(contains(text, "    schedule #2: Medium 0, Soft 7 -> the new best\n"));
    CHECK(contains(text, "    schedule #3: Medium 0, Soft 7 -> #3 in the list\n"));
    CHECK(contains(text, "    schedule #4: Medium 0, Soft 7 -> not kept\n"));
    CHECK(contains(text, "  prune: at best Medium 0, Soft 9 from here, not better than Medium 0, Soft 7, back\n"));
    CHECK(contains(text, "  B: 0 allowed, min 1, slack -1: cannot reach its min any more, back; "
                         "blocked: Alice (no-overlap), Bob (no-overlap)\n"));
}

TEST_CASE("deep in the search the indentation stops growing", "[Printer][D48]") {
    const Problem p = twoShiftsAtOnce();
    Solver solver(p);
    std::ostringstream out;
    TracePrinter trace(out, p, solver.constraints(), 100);

    trace.assigned(TracePrinter::kMaxIndentLevels - 1, 0, 0);   // still indented
    trace.assigned(25, 1, 0);                                   // a number instead
    const std::string pad(2 * TracePrinter::kMaxIndentLevels, ' ');
    CHECK(contains(out.str(), "\n" + pad + "+ Alice\n"));
    CHECK(contains(out.str(), "\n" + pad + "[26] + Bob\n"));
}
