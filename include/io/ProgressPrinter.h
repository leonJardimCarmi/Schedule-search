#pragma once
#include <chrono>
#include <ostream>

#include <nlohmann/json.hpp>

#include "Problem.h"
#include "solver/SearchObserver.h"
#include "solver/Solver.h"

// --progress (stage 8.4, DECISIONS D67): one JSON object per line while the
// search runs, for an app that shows the search moving and lets the manager
// stop it. Written to stderr, so stdout stays for the result (--json -).
//
//   {"event":"start","mode":"best","shifts":88,"employees":12}
//   {"event":"improved","nodes":1840,"ms":12.5,"score":{"medium":4,"soft":640},"place":0}
//   {"event":"found","nodes":26,"ms":0.1}                      (--first)
//   {"event":"tick","nodes":51200,"ms":1000.3,"best":{"medium":4,"soft":585}}
//   {"event":"done","status":"found","nodes":...,"ms":...,"complete":false,"stopped":"request"}
//
// "improved": a schedule entered the list of good schedules (place 0 = the
// new best). "tick": at most once every `tickSeconds`, from the search's
// check every 1024 nodes. Each line is flushed at once.
class ProgressPrinter : public SearchObserver {
public:
    ProgressPrinter(std::ostream& out, const Solver& solver, double tickSeconds = 1.0);

    void start(const Problem& problem, bool firstOnly);
    // status: "found", "noSchedule" or "limitReached", as in the result's run.status.
    void done(const char* status, bool firstOnly);

    void scheduleFound(int depth, const Score* score, int place) override;
    void tick(long long nodes) override;

private:
    double elapsedMs() const;
    void line(const nlohmann::ordered_json& event);

    std::ostream& out_;
    const Solver& solver_;
    double tickSeconds_;
    std::chrono::steady_clock::time_point start_;
    std::chrono::steady_clock::time_point lastTick_;
};
