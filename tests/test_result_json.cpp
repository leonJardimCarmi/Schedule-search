#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "io/JsonLoader.h"
#include "io/ResultJson.h"
#include "report/ReportModel.h"
#include "solver/Solver.h"

// --json (stage 8, D57-D62): the JSON holds exactly what the model and the
// search hold, by ids, with nothing computed on the way. The shape itself is
// checked against contract/result.schema.json by tools/check_contract.py.

using json = nlohmann::ordered_json;

namespace {

// Saturday 2026-11-07 to Monday 2026-11-09: two weeks in the table, the
// first one mostly outside the period. A night shift with a role, a shift
// nobody can work (a hole, D25), and room for several good schedules.
const char* kProblem = R"({
  "period": { "start": "2026-11-07", "end": "2026-11-09" },
  "employees": [
    { "id": 11, "name": "Ann Ash", "maxShifts": 2, "roles": ["guard"],
      "availability": [ { "date": "2026-11-07" , "type": "preferred" }, { "date": "2026-11-08", "type": "available" },
                        { "date": "2026-11-09", "type": "available" } ] },
    { "id": 12, "name": "Ben Bay", "minShifts": 2,
      "availability": [ { "date": "2026-11-07", "type": "available" }, { "date": "2026-11-08", "type": "preferred" },
                        { "date": "2026-11-09", "type": "available" } ] },
    { "id": 13, "name": "Cal Cole",
      "availability": [ { "date": "2026-11-08", "type": "available" }, { "date": "2026-11-09", "type": "available" } ] }
  ],
  "shifts": [
    { "id": "SAT-D", "date": "2026-11-07", "time": "08:00-16:00", "min": 1, "wanted": 2 },
    { "id": "SUN-D", "date": "2026-11-08", "time": "08:00-16:00", "min": 1, "wanted": 2 },
    { "id": "SUN-N", "date": "2026-11-08", "time": "22:00-06:00", "min": 1, "wanted": 1, "role": "guard" },
    { "id": "MON-D", "date": "2026-11-09", "time": "08:00-16:00", "min": 1, "wanted": 1 },
    { "id": "MON-X", "date": "2026-11-09", "time": "08:00-16:00", "min": 1, "wanted": 1, "role": "chef" }
  ]
})";

Problem load() {
    LoadResult r = JsonLoader::loadString(kProblem, "t.json");
    REQUIRE(r.ok());
    return *r.problem;
}

RunInfo bestRun(const Solver& solver) {
    RunInfo run;
    run.nodes = solver.nodesVisited();
    run.complete = solver.complete();
    run.improvements = solver.improvements();
    run.pruned = solver.pruned();
    run.startBound = solver.startBound();
    run.top = 3;
    run.minDiff = 1;
    run.timeLimit = 60;
    return run;
}

}  // namespace

TEST_CASE("the JSON of a result holds the schedules of the search, by ids", "[ResultJson]") {
    const Problem p = load();
    const ScoreModel model = makeScoreModel(p);
    Solver solver(p);
    solver.setTop(3, 1);
    const auto best = solver.findBest(model);
    REQUIRE(best);
    REQUIRE(solver.top().size() >= 2);
    const std::vector<Report> reports = buildReports(p, *best, solver.shortages(), model, solver.top());
    const json j = resultToJson(&p, &reports[0], reports, bestRun(solver), {});

    CHECK(j["formatVersion"] == kResultFormatVersion);
    CHECK(j["run"]["status"] == "found");
    CHECK(j["run"]["mode"] == "best");
    CHECK(j["run"]["complete"] == solver.complete());
    CHECK(j["run"]["nodes"] == solver.nodesVisited());

    // One full schedule per entry of the top list, in its order (D58).
    REQUIRE(j["schedules"].size() == solver.top().size());
    for (std::size_t k = 0; k < solver.top().size(); ++k) {
        const json& s = j["schedules"][k];
        const State& state = solver.top()[k].schedule;
        CHECK(s["rank"] == k + 1);
        CHECK(s["score"]["medium"] == solver.top()[k].score.at(Level::Medium));
        CHECK(s["score"]["soft"] == solver.top()[k].score.at(Level::Soft));
        REQUIRE(s["shifts"].size() == p.shifts.size());
        for (std::size_t i = 0; i < p.shifts.size(); ++i) {
            CHECK(s["shifts"][i]["shift"] == p.shifts[i].id);
            std::vector<int> ids;
            for (const json& person : s["shifts"][i]["staff"]) ids.push_back(person["employee"].get<int>());
            std::vector<int> expected;
            for (int e : state.staffOf(static_cast<int>(i))) expected.push_back(p.employees[e].id);
            CHECK(ids == expected);
        }
        // The best one has no vsBest; each other one says how it differs.
        CHECK(s.contains("vsBest") == (k > 0));
        if (k > 0) CHECK(s["vsBest"]["differences"] == differentAssignments(solver.top()[0].schedule, state));
    }
}

TEST_CASE("the JSON layout puts every shift in the cell of its date and row", "[ResultJson]") {
    const Problem p = load();
    const ScoreModel model = makeScoreModel(p);
    Solver solver(p);
    const auto best = solver.findBest(model);
    REQUIRE(best);
    const std::vector<Report> reports = buildReports(p, *best, solver.shortages(), model, solver.top());
    const json j = resultToJson(&p, &reports[0], reports, bestRun(solver), {});

    const json& weeks = j["layout"]["weeks"];
    REQUIRE(weeks.size() == 2);
    CHECK(weeks[0]["start"] == "2026-11-01");
    CHECK(weeks[0]["days"][6]["date"] == "2026-11-07");
    CHECK(weeks[0]["days"][5]["inPeriod"] == false);
    CHECK(weeks[0]["days"][6]["inPeriod"] == true);
    CHECK(weeks[1]["days"][1]["inPeriod"] == true);    // Monday 2026-11-09, the last day
    CHECK(weeks[1]["days"][2]["inPeriod"] == false);

    std::map<std::string, std::pair<std::string, int>> where;   // shift -> (date, kind)
    for (const json& week : weeks)
        for (std::size_t k = 0; k < week["cells"].size(); ++k)
            for (std::size_t d = 0; d < 7; ++d)
                for (const json& id : week["cells"][k][d]) {
                    CHECK(where.count(id.get<std::string>()) == 0);   // in one cell only
                    where[id.get<std::string>()] = {week["days"][d]["date"].get<std::string>(), static_cast<int>(k)};
                }
    for (const json& shift : j["problem"]["shifts"]) {
        const auto it = where.find(shift["id"].get<std::string>());
        REQUIRE(it != where.end());
        CHECK(it->second.first == shift["date"]);
        CHECK(it->second.second == shift["kind"]);
    }
    // Rows: the night guard and the chef are rows of their own (D44).
    const json& kinds = j["layout"]["kinds"];
    std::set<std::string> rows;
    for (const json& kind : kinds) rows.insert(kind["time"].get<std::string>() + "/" + kind.value("role", ""));
    CHECK(rows == std::set<std::string>{"08:00-16:00/", "08:00-16:00/chef", "22:00-06:00/guard"});
}

TEST_CASE("the JSON leaves out fields that have no value", "[ResultJson]") {
    const Problem p = load();
    const ScoreModel model = makeScoreModel(p);
    Solver solver(p);
    const auto first = solver.findFirst();
    REQUIRE(first);
    const std::vector<Report> reports = buildReports(p, *first, solver.shortages(), model, {});
    RunInfo run;
    run.firstOnly = true;
    const json j = resultToJson(&p, &reports[0], reports, run, {});

    // --first: no proof, no bound, no top list.
    CHECK(j["run"]["mode"] == "first");
    for (const char* key : {"complete", "improvements", "pruned", "startBound"}) CHECK_FALSE(j["run"].contains(key));
    CHECK_FALSE(j["run"]["options"].contains("top"));
    CHECK(j["schedules"].size() == 1);

    const json& ann = j["problem"]["employees"][0];
    const json& cal = j["problem"]["employees"][2];
    CHECK(ann["maxShifts"] == 2);
    CHECK_FALSE(ann.contains("minShifts"));
    CHECK_FALSE(cal.contains("maxShifts"));
    CHECK(cal["roles"].empty());   // a list is written even when empty
    CHECK_FALSE(j["problem"]["shifts"][0].contains("role"));
    CHECK(j["problem"]["shifts"][2]["role"] == "guard");
    CHECK(j["problem"]["shifts"][2]["minutes"] == 480);   // 22:00-06:00 crosses midnight
}

TEST_CASE("the JSON lists the shifts that cannot be staffed, with the reason", "[ResultJson]") {
    const Problem p = load();
    const ScoreModel model = makeScoreModel(p);
    Solver solver(p);
    const auto best = solver.findBest(model);
    REQUIRE(best);
    const std::vector<Report> reports = buildReports(p, *best, solver.shortages(), model, solver.top());
    const json j = resultToJson(&p, &reports[0], reports, bestRun(solver), {});

    REQUIRE(j["shortages"].size() == 1);
    CHECK(j["shortages"][0]["shift"] == "MON-X");
    CHECK(j["shortages"][0]["reason"] == "missingRole");
    CHECK(j["shortages"][0]["candidates"] == 0);
    for (const json& shift : j["schedules"][0]["shifts"])
        if (shift["shift"] == "MON-X") CHECK(shift["status"] == "belowMin");
}

TEST_CASE("the JSON without a schedule still has the problem and its layout", "[ResultJson]") {
    const Problem p = load();
    const ScoreModel model = makeScoreModel(p);
    const State empty(static_cast<int>(p.employees.size()), static_cast<int>(p.shifts.size()));
    const Report shape = buildReport(p, empty, {}, model, {});
    RunInfo run;
    run.status = RunStatus::NoSchedule;
    run.exitCode = 2;
    const json j = resultToJson(&p, &shape, {}, run, {});

    CHECK(j["run"]["status"] == "noSchedule");
    CHECK(j["schedules"].empty());
    CHECK(j["problem"]["shifts"].size() == p.shifts.size());
    CHECK(j["layout"]["weeks"].size() == 2);
}

TEST_CASE("the JSON of an input error has the diagnostics and no problem", "[ResultJson]") {
    LoadResult r = JsonLoader::loadString(R"({ "period": { "start": "2026-02-30", "end": "2026-03-01" } })", "bad.json");
    REQUIRE_FALSE(r.ok());
    RunInfo run;
    run.status = RunStatus::InputError;
    run.exitCode = 1;
    const json j = resultToJson(nullptr, nullptr, {}, run, r.diagnostics.all());

    CHECK(j["run"] == json{{"status", "inputError"}, {"exitCode", 1}});
    CHECK_FALSE(j.contains("problem"));
    CHECK_FALSE(j.contains("layout"));
    CHECK(j["schedules"].empty());
    REQUIRE_FALSE(j["diagnostics"].empty());
    CHECK(j["diagnostics"][0]["severity"] == "error");
    CHECK(j["diagnostics"][0]["where"].get<std::string>().rfind("bad.json", 0) == 0);
}

TEST_CASE("the JSON goes to a stream for \"-\", and a bad path is an error", "[ResultJson]") {
    RunInfo run;
    run.status = RunStatus::InputError;
    run.exitCode = 1;
    const json j = resultToJson(nullptr, nullptr, {}, run, {});
    std::ostringstream out;
    Diagnostics problems;
    CHECK(writeResultJson("-", j, out, problems));
    CHECK(json::parse(out.str()) == j);
    CHECK(out.str().back() == '\n');

    std::ostringstream unused;
    CHECK_FALSE(writeResultJson("no/such/folder/result.json", j, unused, problems));
    CHECK(problems.hasErrors());
    CHECK(unused.str().empty());
}
