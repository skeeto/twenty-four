// Standalone unit tests for the game model (no SDL). Build:
//   c++ -std=c++20 -I src -I build/generated tools/test_game.cpp src/game.cpp -o /tmp/test_game
#include "game.hpp"
#include "puzzles_generated.h"

#include <cstdio>
#include <set>
#include <string>

using namespace tf;

static int failures = 0;
#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } \
    } while (0)

// Build a Game whose board holds the four given values (all present).
static Game board4(long long a, long long b, long long c, long long d) {
    Game g;
    char buf[256];
    snprintf(buf, sizeof buf,
             "TF1\n0 0 0 1\n0\n1 %lld 1 1 %lld 1 1 %lld 1 1 %lld 1\n0\n", a, b, c, d);
    CHECK(g.deserialize(buf));
    return g;
}

// Build a Game with only slots 0 and 1 present.
static Game board2(long long a, long long b) {
    Game g;
    char buf[256];
    snprintf(buf, sizeof buf,
             "TF1\n0 0 0 1\n0\n1 %lld 1 1 %lld 1 0 0 1 0 0 1\n0\n", a, b);
    CHECK(g.deserialize(buf));
    return g;
}

int main() {
    // result = target(dst) OP dragged(src)
    {
        Game g = board4(6, 4, 2, 1);
        CHECK(g.board().count() == 4);
        auto r = g.apply(0, 1, Op::Mul);  // 6 * 4 = 24
        CHECK(r.ok && r.result == Rational(24));
        CHECK(g.board().count() == 3 && !g.board().present[0]);
        CHECK(g.board().value[1] == Rational(24));
    }
    { Game g = board4(6, 4, 2, 1); auto r = g.apply(1, 0, Op::Sub); CHECK(r.ok && r.result == Rational(-2)); }  // dragged 4 - target 6
    { Game g = board4(6, 4, 2, 1); auto r = g.apply(0, 1, Op::Sub); CHECK(r.ok && r.result == Rational(2)); }   // dragged 6 - target 4
    { Game g = board4(6, 4, 2, 1); auto r = g.apply(0, 1, Op::Add); CHECK(r.ok && r.result == Rational(10)); }
    { Game g = board4(6, 4, 2, 1); auto r = g.apply(0, 2, Op::Div); CHECK(r.ok && r.result == Rational(3)); }   // dragged 6 / target 2
    {  // fraction rejected, board unchanged
        Game g = board4(6, 4, 2, 1);
        auto r = g.apply(0, 1, Op::Div);  // dragged 6 / target 4
        CHECK(!r.ok && g.board().count() == 4);
    }
    {  // divide by zero rejected (target is the divisor)
        Game g = board4(6, 4, 2, 0);
        auto r = g.apply(0, 3, Op::Div);  // dragged 6 / target 0
        CHECK(!r.ok && g.board().count() == 4);
    }

    // win detection + score
    {
        Game g = board2(4, 6);
        auto r = g.apply(0, 1, Op::Mul);  // 6*4 = 24, single tile left
        CHECK(r.ok && r.won);
        CHECK(g.successCount() == 1);
    }
    {  // single tile that is not 24 is not a win
        Game g = board2(4, 5);
        auto r = g.apply(0, 1, Op::Mul);  // 20
        CHECK(r.ok && !r.won && g.successCount() == 0);
    }

    // undo restores the previous board
    {
        Game g = board4(6, 4, 2, 1);
        g.apply(0, 1, Op::Mul);
        CHECK(g.canUndo() && g.board().count() == 3);
        g.undo();
        CHECK(g.board().count() == 4);
        CHECK(g.board().value[0] == Rational(6) && g.board().value[1] == Rational(4));
        CHECK(!g.canUndo());
    }

    // grab bag: a full cycle visits every puzzle exactly once
    {
        Game g;
        std::set<int> seen;
        seen.insert(g.currentPuzzle());
        for (int i = 1; i < kPuzzleCount; ++i) { g.newPuzzle(); seen.insert(g.currentPuzzle()); }
        CHECK((int)seen.size() == kPuzzleCount);
        g.newPuzzle();  // refill, must stay valid
        CHECK(g.currentPuzzle() >= 0 && g.currentPuzzle() < kPuzzleCount);
    }

    // serialize / deserialize round-trips exactly
    {
        Game g = board4(6, 4, 2, 1);
        g.apply(0, 1, Op::Mul);
        std::string s = g.serialize();
        Game g2;
        CHECK(g2.deserialize(s));
        CHECK(g2.serialize() == s);
        CHECK(g2.board().count() == g.board().count());
    }

    if (failures == 0) printf("ALL TESTS PASSED\n");
    else printf("%d CHECK(S) FAILED\n", failures);
    return failures ? 1 : 0;
}
