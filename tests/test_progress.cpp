#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <sstream>
#include <string>
#include <vector>

#include "io/JsonLoader.h"
#include "io/ProgressPrinter.h"
#include "io/ResultJson.h"
#include "solver/Solver.h"

// Stage 8.4 (D67): stopping a search on request, and --progress.

using json = nlohmann::ordered_json;

namespace {

// `days` days, a morning and an evening each (min 1, wanted 2), and
// `people` employees available every day. 4 days and 4 people, with a list
// of 3 schedules (setTop(3, 1)), take 2,615 nodes to search completely:
// more than one check (every 1024 nodes).
Problem everyoneEveryDay(int days, int people) {
    json p;
    std::vector<std::string> dates;
    for (int d = 0; d < days; ++d) dates.push_back((8 + d < 10 ? "2026-11-0" : "2026-11-") + std::to_string(8 + d));
    p["period"] = {{"start", dates.front()}, {"end", dates.back()}};
    p["employees"] = json::array();
    for (int e = 1; e <= people; ++e) {
        json availability = json::array();
        for (const std::string& d : dates) availability.push_back({{"date", d}, {"type", "available"}});
        p["employees"].push_back({{"id", e}, {"name", "E" + std::to_string(e)}, {"availability", availability}});
    }
    p["shifts"] = json::array();
    for (const std::string& d : dates) {
        p["shifts"].push_back({{"id", d + "-M"}, {"date", d}, {"time", "08:00-16:00"}, {"min", 1}, {"wanted", 2}});
        p["shifts"].push_back({{"id", d + "-E"}, {"date", d}, {"time", "16:00-23:00"}, {"min", 1}, {"wanted", 2}});
    }
    LoadResult r = JsonLoader::loadString(p.dump(), "t.json");
    REQUIRE(r.ok());
    return *r.problem;
}

std::vector<json> lines(const std::string& text) {
    std::vector<json> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) out.push_back(json::parse(line));
    return out;
}

}  // namespace

TEST_CASE("a stop request ends the search at its next check, like a limit", "[Stop]") {
    const Problem p = everyoneEveryDay(4, 4);
    const ScoreModel model = makeScoreModel(p);

    Solver free(p);   // without a request: the whole search
    free.setTop(3, 1);
    REQUIRE(free.findBest(model));
    REQUIRE(free.complete());
    REQUIRE(free.nodesVisited() > 1024);
    CHECK(free.stopReason() == StopReason::None);

    std::atomic<bool> stop{true};   // asked before the search even starts
    Solver stopped(p);
    stopped.setTop(3, 1);
    stopped.setLimits(SearchLimits{std::nullopt, std::nullopt, &stop});
    const auto best = stopped.findBest(model);
    CHECK_FALSE(stopped.complete());
    CHECK(stopped.stopReason() == StopReason::Request);
    CHECK(stopped.nodesVisited() == 1024);   // the first check
    // Anytime (D31): what was found before the check is the result.
    CHECK(best.has_value() == (stopped.status() == SearchStatus::Found));
}

TEST_CASE("a search shorter than one check is complete, even if stop was asked", "[Stop]") {
    const Problem p = everyoneEveryDay(1, 2);
    std::atomic<bool> stop{true};
    Solver solver(p);
    solver.setLimits(SearchLimits{std::nullopt, std::nullopt, &stop});
    REQUIRE(solver.findBest(makeScoreModel(p)));
    CHECK(solver.nodesVisited() < 1024);
    CHECK(solver.complete());
    CHECK(solver.stopReason() == StopReason::None);
}

TEST_CASE("the search says which limit stopped it", "[Stop]") {
    const Problem p = everyoneEveryDay(4, 4);
    const ScoreModel model = makeScoreModel(p);
    Solver byNodes(p);
    byNodes.setLimits(SearchLimits{std::nullopt, 100LL, nullptr});
    byNodes.findBest(model);
    CHECK(byNodes.stopReason() == StopReason::Nodes);

    Solver byTime(p);
    byTime.setTop(3, 1);
    byTime.setLimits(SearchLimits{1e-9, std::nullopt, nullptr});
    byTime.findBest(model);
    CHECK(byTime.stopReason() == StopReason::Time);

    // A fresh search forgets the last reason.
    byNodes.setLimits(SearchLimits{});
    byNodes.findBest(model);
    CHECK(byNodes.stopReason() == StopReason::None);
}

TEST_CASE("progress (--progress) writes a JSON line per event: start, improvements, ticks, done", "[Progress]") {
    const Problem p = everyoneEveryDay(4, 4);
    Solver solver(p);
    solver.setTop(3, 1);
    std::ostringstream out;
    ProgressPrinter progress(out, solver, 0.0);   // a tick at every check
    progress.start(p, false);
    solver.setObserver(&progress);
    REQUIRE(solver.findBest(makeScoreModel(p)));
    progress.done("found", false);

    const std::vector<json> events = lines(out.str());
    REQUIRE(events.size() >= 3);
    CHECK(events.front() == json{{"event", "start"}, {"mode", "best"}, {"shifts", 8}, {"employees", 4}});
    const json& done = events.back();
    CHECK(done["event"] == "done");
    CHECK(done["status"] == "found");
    CHECK(done["complete"] == true);
    CHECK_FALSE(done.contains("stopped"));
    CHECK(done["nodes"] == solver.nodesVisited());

    long long newBest = 0, ticks = 0, lastNodes = 0;
    for (const json& e : events) {
        if (e["event"] == "improved" && e["place"] == 0) ++newBest;
        if (e["event"] == "tick") {
            ++ticks;
            CHECK(e["nodes"].get<long long>() % 1024 == 0);
            CHECK(e.contains("best"));
        }
        if (e.contains("nodes")) {   // in the order they happened
            CHECK(e["nodes"].get<long long>() >= lastNodes);
            lastNodes = e["nodes"].get<long long>();
        }
    }
    CHECK(newBest == solver.improvements());
    CHECK(ticks == solver.nodesVisited() / 1024);
}

TEST_CASE("progress (--progress) with --first reports the schedule found, and the stop reason when stopped", "[Progress]") {
    const Problem p = everyoneEveryDay(4, 4);
    Solver solver(p);
    std::ostringstream out;
    ProgressPrinter progress(out, solver);
    progress.start(p, true);
    solver.setObserver(&progress);
    REQUIRE(solver.findFirst());
    progress.done("found", true);
    std::vector<json> events = lines(out.str());
    REQUIRE(events.size() == 3);
    CHECK(events[1]["event"] == "found");
    CHECK_FALSE(events[2].contains("complete"));   // no meaning with --first

    std::atomic<bool> stop{true};
    Solver stopped(p);
    stopped.setTop(3, 1);
    stopped.setLimits(SearchLimits{std::nullopt, std::nullopt, &stop});
    std::ostringstream out2;
    ProgressPrinter progress2(out2, stopped);
    stopped.setObserver(&progress2);
    stopped.findBest(makeScoreModel(p));
    progress2.done("found", false);
    events = lines(out2.str());
    CHECK(events.back()["stopped"] == "request");
    CHECK(events.back()["complete"] == false);
}

TEST_CASE("the JSON result says why the search stopped", "[ResultJson][Stop]") {
    RunInfo run;
    run.status = RunStatus::NoSchedule;
    run.exitCode = 2;
    const Problem p = everyoneEveryDay(1, 2);
    const State empty(static_cast<int>(p.employees.size()), static_cast<int>(p.shifts.size()));
    const Report shape = buildReport(p, empty, {}, makeScoreModel(p), {});
    CHECK_FALSE(resultToJson(&p, &shape, {}, run, {})["run"].contains("stopped"));
    run.status = RunStatus::LimitReached;
    run.exitCode = 3;
    run.stopped = StopReason::Request;
    CHECK(resultToJson(&p, &shape, {}, run, {})["run"]["stopped"] == "request");
}
