#include "io/ProgressPrinter.h"

#include <cmath>

#include "io/ResultJson.h"

using json = nlohmann::ordered_json;

namespace {

json scoreJson(const Score& score) {
    return json{{"medium", score.at(Level::Medium)}, {"soft", score.at(Level::Soft)}};
}

}  // namespace

ProgressPrinter::ProgressPrinter(std::ostream& out, const Solver& solver, double tickSeconds)
    : out_(out), solver_(solver), tickSeconds_(tickSeconds),
      start_(std::chrono::steady_clock::now()), lastTick_(start_) {}

double ProgressPrinter::elapsedMs() const {
    const std::chrono::duration<double, std::milli> ms = std::chrono::steady_clock::now() - start_;
    return std::round(ms.count() * 10) / 10;
}

void ProgressPrinter::line(const json& event) {
    out_ << event.dump(-1, ' ', false, json::error_handler_t::replace) << '\n' << std::flush;
}

void ProgressPrinter::start(const Problem& problem, bool firstOnly) {
    start_ = lastTick_ = std::chrono::steady_clock::now();
    line(json{{"event", "start"}, {"mode", firstOnly ? "first" : "best"},
              {"shifts", problem.shifts.size()}, {"employees", problem.employees.size()}});
}

void ProgressPrinter::scheduleFound(int /*depth*/, const Score* score, int place) {
    if (!score) {   // --first: the first legal schedule, the search ends here
        line(json{{"event", "found"}, {"nodes", solver_.nodesVisited()}, {"ms", elapsedMs()}});
        return;
    }
    if (place < 0) return;   // not good enough for the list
    line(json{{"event", "improved"}, {"nodes", solver_.nodesVisited()}, {"ms", elapsedMs()},
              {"score", scoreJson(*score)}, {"place", place}});
}

void ProgressPrinter::tick(long long nodes) {
    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(now - lastTick_).count() < tickSeconds_) return;
    lastTick_ = now;
    json event{{"event", "tick"}, {"nodes", nodes}, {"ms", elapsedMs()}};
    if (!solver_.top().empty()) event["best"] = scoreJson(solver_.top().front().score);
    line(event);
}

void ProgressPrinter::done(const char* status, bool firstOnly) {
    json event{{"event", "done"}, {"status", status}, {"nodes", solver_.nodesVisited()}, {"ms", elapsedMs()}};
    if (!firstOnly) event["complete"] = solver_.complete();
    if (solver_.stopReason() != StopReason::None) event["stopped"] = stopReasonName(solver_.stopReason());
    line(event);
}
