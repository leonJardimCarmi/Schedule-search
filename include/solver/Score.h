#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "Problem.h"
#include "solver/State.h"

// Scoring a legal schedule (stage 5). Lower is better.
//
// Hard rules are Constraints: a schedule that breaks one is never produced.
// Everything else that makes a schedule better or worse is a SoftRule here.
// The design is open (DECISIONS D32): a new rule is one class plus one line
// in makeScoreModel(), and the search does not change.

// Levels are compared in order (D29): any difference in Medium decides
// before Soft is even looked at.
enum class Level { Medium = 0, Soft = 1 };
constexpr int kLevelCount = 2;
const char* toString(Level level);

struct Score {
    std::array<long long, kLevelCount> total{};   // weighted penalty per level

    long long at(Level level) const { return total[static_cast<int>(level)]; }
    // std::array compares element by element, in order: exactly D29.
    bool operator<(const Score& other) const { return total < other.total; }
    bool operator==(const Score& other) const { return total == other.total; }
};

// What the search knows in the middle of a branch, for lowerBound().
// Shifts are filled one at a time, so a shift is either decided (its staff
// is final in this branch) or still empty.
struct SearchView {
    const std::vector<char>& decided;                  // [shift] -> staff is final
    const std::vector<std::vector<int>>& candidates;   // [shift] -> employees who fit
    const std::vector<std::vector<char>>& allowedNow;  // [shift][pos] -> hard rules allow it now
    const std::vector<int>& allowedCount;              // [shift] -> how many are allowed now
    const std::vector<int>& searchMin;                 // [shift] -> at least this many will be assigned
};
// The hard rules only get stricter as people are assigned: someone who is
// not allowed in a shift now will not be allowed later in this branch.
// So an empty shift ends with at most allowedCount people. That is the
// fact most bounds are built on.

// One kind of "this could be better".
class SoftRule {
public:
    virtual ~SoftRule() = default;
    virtual const char* name() const = 0;          // the key in "weights", e.g. "double"
    virtual const char* description() const = 0;   // what one unit of violation means
    virtual Level level() const { return Level::Soft; }

    // How many units of violation the complete schedule has. Each rule
    // decides how to count: people missing, doubles, a fairness measure...
    virtual long long violations(const Problem& problem, const State& schedule) const = 0;

    // Optional (step 5.3): at least how many violations ANY completion of
    // this partial schedule will have. It must never be too high, or the
    // search may cut off the best schedule. 0 is always correct, so a rule
    // that cannot say anything useful does not need to override this.
    virtual long long lowerBound(const Problem& /*problem*/, const State& /*partial*/,
                                 const SearchView& /*view*/) const { return 0; }
};

// One line of the explanation of a score.
struct ScoreLine {
    std::string rule;
    std::string description;
    Level level;
    long long violations;
    int weight;
    long long penalty;   // violations * weight
};

class ScoreModel {
public:
    void add(std::unique_ptr<SoftRule> rule, int weight);

    Score score(const Problem& problem, const State& schedule) const;
    std::vector<ScoreLine> breakdown(const Problem& problem, const State& schedule) const;

    // The best score any completion of a partial schedule could still get.
    Score lowerBound(const Problem& problem, const State& partial, const SearchView& view) const;

    // Names in the input's "weights" that match no rule (probably typos).
    const std::vector<std::string>& unknownWeights() const { return unknownWeights_; }

private:
    struct Entry {
        std::unique_ptr<SoftRule> rule;
        int weight;
    };
    std::vector<Entry> entries_;
    std::vector<std::string> unknownWeights_;

    friend ScoreModel makeScoreModel(const Problem& problem);
};

// All soft rules with their default weights, overridden by the input's
// "weights" (D28). A weight of 0 turns a rule off.
ScoreModel makeScoreModel(const Problem& problem);
