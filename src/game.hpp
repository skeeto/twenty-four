#pragma once
#include "rational.hpp"
#include <array>
#include <random>
#include <string>
#include <vector>

namespace tf {

enum class Op { None, Add, Sub, Mul, Div };

// Logical board: up to four numbers living in fixed 2x2 slots (0=TL,1=TR,2=BL,
// 3=BR). Merging removes the dragged slot and updates the target slot.
struct Board {
    std::array<Rational, 4> value{};
    std::array<bool, 4> present{};
    int count() const {
        int c = 0;
        for (bool b : present) c += b ? 1 : 0;
        return c;
    }
};

struct ApplyResult {
    bool ok = false;     // operation was legal (integer result, no /0)
    bool won = false;    // board collapsed to a single 24
    Rational result;     // resulting value placed in the target slot
};

// Game model: current board, undo history, grab-bag puzzle picker, score.
// Holds no animation/visual state; the app layer drives the juice.
class Game {
public:
    Game();

    void newPuzzle();                            // pull the next puzzle from the bag
    ApplyResult apply(int src, int dst, Op op);  // src dragged onto dst
    bool canUndo() const { return !undo_.empty(); }
    void undo();

    const Board& board() const { return board_; }
    int successCount() const { return success_; }
    int currentPuzzle() const { return current_; }

    std::string serialize() const;
    bool deserialize(const std::string& s);

    // If a restored board is already solved (single 24), start fresh instead.
    void ensurePlayable();

private:
    void refillBag();
    int nextFromBag();

    Board board_{};
    std::vector<Board> undo_;
    std::vector<int> bag_;
    int bagPos_ = 0;
    int current_ = -1;
    int success_ = 0;
    std::mt19937 rng_;
};

}  // namespace tf
