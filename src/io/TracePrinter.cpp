#include "io/TracePrinter.h"

#include "io/Printer.h"

TracePrinter::TracePrinter(std::ostream& out, const Problem& problem, const ConstraintList& constraints,
                           int maxLines)
    : out_(out), problem_(problem), constraints_(constraints), names_(shortNames(problem)), maxLines_(maxLines) {
    out_ << "--- trace: one level of indentation per shift already decided ---\n";
}

bool TracePrinter::startLine(int depth) {
    if (lines_ >= maxLines_) {
        ++leftOut_;
        return false;
    }
    ++lines_;
    if (depth <= kMaxIndentLevels) {
        out_ << std::string(2 * static_cast<std::size_t>(depth), ' ');
    } else {
        out_ << std::string(2 * kMaxIndentLevels, ' ') << '[' << depth << "] ";
    }
    return true;
}

std::string TracePrinter::blocked(const State& state, const ShiftChoice& choice) const {
    std::string text;
    for (int emp : *choice.candidates) {
        std::string rules;
        for (const auto& constraint : constraints_)
            if (!constraint->allows(problem_, state, emp, choice.shift))
                rules += (rules.empty() ? "" : ", ") + std::string(constraint->name());
        if (!rules.empty()) text += (text.empty() ? "" : ", ") + names_[emp] + " (" + rules + ")";
    }
    return text;
}

std::string TracePrinter::choiceText(const ShiftChoice& c) const {
    return problem_.shifts[c.shift].id + ": " + std::to_string(c.allowed) + " allowed, min " +
           std::to_string(c.min) + ", slack " + std::to_string(c.slack);
}

void TracePrinter::shiftChosen(const State& state, int depth, const ShiftChoice& c) {
    if (!startLine(depth)) return;
    out_ << choiceText(c);
    if (c.open > 1) out_ << " (the least of " << c.open << " open)";
    else out_ << " (the last one open)";
    const std::string who = blocked(state, c);
    if (!who.empty()) out_ << "; blocked: " << who;
    out_ << '\n';
}

void TracePrinter::deadEnd(const State& state, int depth, const ShiftChoice& c) {
    if (!startLine(depth)) return;
    out_ << choiceText(c) << ": cannot reach its min any more, back";
    const std::string who = blocked(state, c);
    if (!who.empty()) out_ << "; blocked: " << who;
    out_ << '\n';
}

void TracePrinter::sizeTried(int depth, int shift, int target) {
    if (!startLine(depth + 1)) return;
    out_ << problem_.shifts[shift].id << ": try " << target << (target == 1 ? " person" : " people") << '\n';
}

void TracePrinter::assigned(int depth, int emp, int /*shift*/) {
    if (!startLine(depth + 1)) return;
    out_ << "+ " << names_[emp] << '\n';
}

void TracePrinter::unassigned(int depth, int emp, int /*shift*/) {
    if (!startLine(depth + 1)) return;
    out_ << "- " << names_[emp] << '\n';
}

void TracePrinter::pruned(int depth, const Score& bound, const Score& toBeat) {
    if (!startLine(depth)) return;
    out_ << "prune: at best " << scoreText(bound) << " from here, not better than " << scoreText(toBeat)
         << ", back\n";
}

void TracePrinter::scheduleFound(int depth, const Score* score, int place) {
    ++schedules_;
    if (!startLine(depth)) return;
    out_ << "schedule #" << schedules_;
    if (score) {
        out_ << ": " << scoreText(*score) << " -> ";
        if (place == 0) out_ << "the new best";
        else if (place > 0) out_ << "#" << place + 1 << " in the list";
        else out_ << "not kept";
    }
    out_ << '\n';
}

void TracePrinter::finish() {
    if (leftOut_ == 0)
        out_ << "--- end of trace: the whole search, " << lines_ << " lines ---\n\n";
    else
        out_ << "--- trace stopped after " << lines_ << " lines; the search went on for " << leftOut_
             << " more steps (--trace-lines <n> shows more) ---\n\n";
}
