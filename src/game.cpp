#include "game.hpp"
#include "puzzles_generated.h"

#include <algorithm>
#include <sstream>

namespace tf {

Game::Game() : rng_(std::random_device{}()) {
    refillBag();
    newPuzzle();
}

void Game::refillBag() {
    bag_.resize(kPuzzleCount);
    for (int i = 0; i < kPuzzleCount; ++i) bag_[i] = i;
    std::shuffle(bag_.begin(), bag_.end(), rng_);
    // Avoid replaying the puzzle we just finished as the first of the new bag.
    if (kPuzzleCount > 1 && bag_.front() == current_)
        std::swap(bag_.front(), bag_.back());
    bagPos_ = 0;
}

int Game::nextFromBag() {
    if (bagPos_ >= static_cast<int>(bag_.size())) refillBag();
    return bag_[bagPos_++];
}

void Game::newPuzzle() {
    current_ = nextFromBag();
    board_ = Board{};
    int order[4] = {0, 1, 2, 3};
    std::shuffle(order, order + 4, rng_);  // don't always present them ascending
    for (int i = 0; i < 4; ++i) {
        board_.value[i] = Rational(kPuzzles[current_][order[i]]);
        board_.present[i] = true;
    }
    undo_.clear();
}

ApplyResult Game::apply(int src, int dst, Op op) {
    ApplyResult r;
    if (src < 0 || src > 3 || dst < 0 || dst > 3 || src == dst) return r;
    if (!board_.present[src] || !board_.present[dst]) return r;

    const Rational a = board_.value[src];  // dragged = left operand
    const Rational b = board_.value[dst];  // stationary target = right operand
    Rational result;
    switch (op) {
        case Op::Add: result = a + b; break;
        case Op::Sub: result = a - b; break;
        case Op::Mul: result = a * b; break;
        case Op::Div:
            if (b.isZero()) return r;  // dividing by zero is the only illegal move
            result = a / b;            // fractions are allowed; a cell may hold e.g. 3/4
            break;
        default: return r;
    }

    undo_.push_back(board_);
    board_.value[dst] = result;
    board_.present[src] = false;

    r.ok = true;
    r.result = result;
    if (board_.count() == 1 && result == Rational(24)) {
        r.won = true;
        ++success_;
    }
    return r;
}

void Game::undo() {
    if (undo_.empty()) return;
    board_ = undo_.back();
    undo_.pop_back();
}

void Game::ensurePlayable() {
    if (board_.count() <= 1) newPuzzle();
}

// --- serialization -------------------------------------------------------
// Plain whitespace-delimited tokens, versioned, easy to parse and diff.

std::string Game::serialize() const {
    std::ostringstream o;
    o << "TF1\n";
    o << success_ << ' ' << current_ << ' ' << bagPos_ << ' ' << bag_.size() << '\n';
    for (size_t i = 0; i < bag_.size(); ++i) o << bag_[i] << (i + 1 < bag_.size() ? ' ' : '\n');
    auto writeBoard = [&o](const Board& b) {
        for (int i = 0; i < 4; ++i)
            o << (b.present[i] ? 1 : 0) << ' ' << b.value[i].num << ' ' << b.value[i].den << ' ';
        o << '\n';
    };
    writeBoard(board_);
    o << undo_.size() << '\n';
    for (const auto& b : undo_) writeBoard(b);
    return o.str();
}

bool Game::deserialize(const std::string& s) {
    std::istringstream in(s);
    std::string tag;
    in >> tag;
    if (tag != "TF1") return false;

    int bagSize = 0;
    if (!(in >> success_ >> current_ >> bagPos_ >> bagSize)) return false;
    if (bagSize < 0 || bagSize > 100000) return false;
    bag_.resize(bagSize);
    for (int i = 0; i < bagSize; ++i)
        if (!(in >> bag_[i])) return false;

    auto readBoard = [&in](Board& b) -> bool {
        for (int i = 0; i < 4; ++i) {
            int present = 0;
            long long n = 0, d = 1;
            if (!(in >> present >> n >> d)) return false;
            b.present[i] = present != 0;
            b.value[i] = Rational(n, d);
        }
        return true;
    };
    if (!readBoard(board_)) return false;

    size_t undoCount = 0;
    if (!(in >> undoCount)) return false;
    if (undoCount > 100000) return false;
    undo_.clear();
    undo_.reserve(undoCount);
    for (size_t i = 0; i < undoCount; ++i) {
        Board b;
        if (!readBoard(b)) return false;
        undo_.push_back(b);
    }
    return true;
}

}  // namespace tf
