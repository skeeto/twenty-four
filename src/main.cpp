#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "anim.hpp"
#include "audio.hpp"
#include "game.hpp"
#include "render.hpp"
#include "storage.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
// Web: drive the canvas CSS cursor so we get true open/closed-hand "grab".
EM_JS(void, tf_set_cursor, (const char* css), {
    var c = Module['canvas'];
    if (c) c.style.cursor = UTF8ToString(css);
});
#else
#include "icon_generated.h"  // embedded window-icon PNG bytes
#include "stb_image.h"
#endif

#ifndef TF_VERSION
#define TF_VERSION "1.0.0"
#endif

using namespace tf;

namespace {

// ---- logical layout (resolution-independent; letterboxed) ----------------
constexpr float LW = 600, LH = 720;
constexpr float FX = 28, FY = 104, FS = 544, BAND = 70, GAP = 20;
constexpr float INNER = FX + BAND;
constexpr float CELL = (FS - 2 * BAND - GAP) / 2;
constexpr float BOX = 176, BOX_RAD = 30;

constexpr float MERGE_DUR = 0.18f, REJECT_DUR = 0.24f, CELEBRATE_DUR = 1.6f;
constexpr float POP_DUR = 0.4f, SPAWN_DUR = 0.45f;

constexpr Vec2 kUndoCenter{LW - 62.0f, 46.0f};  // top-right: easy to reach with a right thumb
constexpr Vec2 kScoreCenter{64.0f, 46.0f};      // top-left

Vec2 slotCenter(int i) {
    int col = i % 2, row = i / 2;
    return {INNER + col * (CELL + GAP) + CELL / 2, (FY + BAND) + row * (CELL + GAP) + CELL / 2};
}
Vec2 zoneCenter(Op op) {
    const float m = (BAND - 10) / 2;  // center the glyph in the visible border strip
    switch (op) {
        case Op::Add: return {FX + FS / 2, FY + m};
        case Op::Sub: return {FX + FS / 2, FY + FS - m};
        case Op::Div: return {FX + m, FY + FS / 2};
        case Op::Mul: return {FX + FS - m, FY + FS / 2};
        default: return {0, 0};
    }
}

// ---- palette -------------------------------------------------------------
const Color BG_TOP{0.965f, 0.95f, 1.0f, 1}, BG_BOT{0.87f, 0.89f, 0.99f, 1};
const Color PANEL_TOP{1, 1, 1, 1}, PANEL_BOT{0.93f, 0.94f, 0.99f, 1};
const Color RECESS_TOP{0.88f, 0.89f, 0.96f, 1}, RECESS_BOT{0.95f, 0.96f, 1.0f, 1};
const Color BOX_TOP{1, 1, 1, 1}, BOX_BOT{0.86f, 0.88f, 0.98f, 1};
const Color BOX_TEXT{0.27f, 0.22f, 0.45f, 1};
const Color SHADOW{0.22f, 0.20f, 0.42f, 0.45f};
const Color GOLD{1.0f, 0.80f, 0.25f, 1};
const Color WHITE{1, 1, 1, 1};

Color opColor(Op op) {
    switch (op) {
        case Op::Add: return {0.27f, 0.80f, 0.52f, 1};
        case Op::Sub: return {1.0f, 0.60f, 0.30f, 1};
        case Op::Mul: return {0.36f, 0.62f, 1.0f, 1};
        case Op::Div: return {0.86f, 0.42f, 0.82f, 1};
        default: return WHITE;
    }
}

float frand() { return (float)std::rand() / (float)RAND_MAX; }
float frand(float a, float b) { return a + (b - a) * frand(); }

float popScale(float t) {  // single juicy overshoot, settles to 1
    if (t >= 1) return 1.0f;
    return 1.0f + 0.30f * (1.0f - ease::outElastic(saturate(t)));
}

}  // namespace

struct Confetti {
    Vec2 pos, vel;
    Color col;
    float rot, rotVel, life, maxLife, size;
};

struct App {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    Renderer rdr;
    Audio audio;
    Game game;

    enum class Phase { Idle, Dragging, Merging, Rejecting, Celebrating };
    Phase phase = Phase::Idle;

    Vec2 pointer{}, grabOffset{}, dragDir{};
    int dragSrc = -1, hoverTarget = -1, lastTarget = -1;
    Op pendingOp = Op::None;

    int mergeSrc = -1, mergeDst = -1;
    Rational mergeSrcValue;
    Vec2 mergeStart{};
    float mergeT = 0;
    bool pendingWin = false;

    int rejectSlot = -1;
    Rational rejectValue;
    Vec2 rejectFrom{};
    float rejectT = 0;
    int shakeSlot = -1;
    float shakeT = 1;

    float popT[4] = {1, 1, 1, 1};
    float spawnT[4] = {1, 1, 1, 1};

    float celebrateT = 0;
    float displayCount = 0;
    float countPopT = 1;
    std::vector<Confetti> confetti;

    float undoPressT = 1;
    Uint64 lastTick = 0;
    bool audioUnlocked = false;

    // Mouse cursor affordance: plain arrow, hand over a grabbable cell/button,
    // closed/grabbing hand while dragging. (Touch shows no cursor, so this is
    // a no-op there.)
    enum class CursorKind { Default, Hover, Grab };
    CursorKind cursorKind = CursorKind::Default;
#ifndef __EMSCRIPTEN__
    SDL_Cursor* cursors[3] = {nullptr, nullptr, nullptr};
#endif

    // --- helpers ---------------------------------------------------------
    void save() { storage::save(game.serialize()); }

    void initCursors() {
#ifndef __EMSCRIPTEN__
        cursors[(int)CursorKind::Default] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        cursors[(int)CursorKind::Hover]   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
        cursors[(int)CursorKind::Grab]    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE);
#endif
    }
    void destroyCursors() {
#ifndef __EMSCRIPTEN__
        for (SDL_Cursor* c : cursors)
            if (c) SDL_DestroyCursor(c);
#endif
    }
    void setCursor(CursorKind k) {
        if (k == cursorKind) return;
        cursorKind = k;
#ifdef __EMSCRIPTEN__
        tf_set_cursor(k == CursorKind::Grab ? "grabbing"
                    : k == CursorKind::Hover ? "grab"
                                             : "default");
#else
        if (cursors[(int)k]) SDL_SetCursor(cursors[(int)k]);
#endif
    }

    // Choose the cursor from the current interaction state. Called each frame.
    void updateCursor() {
        CursorKind k = CursorKind::Default;
        if (phase == Phase::Dragging) {
            k = CursorKind::Grab;
        } else if (phase == Phase::Idle && (tileUnder(pointer) >= 0 ||
                                            (overUndo(pointer) && game.canUndo()))) {
            k = CursorKind::Hover;
        }
        setCursor(k);
    }

    void resetSpawn() {
        for (int i = 0; i < 4; ++i) {
            spawnT[i] = -0.07f * i;  // staggered pop-in
            popT[i] = 1;
        }
    }

    void unlockAudio() {
        if (!audioUnlocked) { audio.resume(); audioUnlocked = true; }
    }

    int tileUnder(Vec2 p) const {
        for (int i = 0; i < 4; ++i) {
            if (!game.board().present[i]) continue;
            Vec2 c = slotCenter(i);
            if (std::fabs(p.x - c.x) <= BOX / 2 && std::fabs(p.y - c.y) <= BOX / 2) return i;
        }
        return -1;
    }

    bool overUndo(Vec2 p) const {
        return length(p - kUndoCenter) <= 34;
    }

    void updateHover() {
        int t = tileUnder(pointer);
        if (t == dragSrc) t = -1;
        if (t >= 0) lastTarget = t;               // remember the last cell we were actually over
        hoverTarget = (t >= 0) ? t : lastTarget;  // forgiving: keep it if the flick ends in the border
        pendingOp = Op::None;
        if (hoverTarget < 0) return;
        // The operator is chosen by the direction the user is dragging toward
        // (the border they're heading for), not by where in the cell they land.
        if (length(dragDir) < 0.8f) return;
        if (std::fabs(dragDir.x) > std::fabs(dragDir.y))
            pendingOp = dragDir.x > 0 ? Op::Mul : Op::Div;
        else
            pendingOp = dragDir.y > 0 ? Op::Sub : Op::Add;
    }

    void pointerDown(Vec2 p) {
        pointer = p;
        unlockAudio();
        if (phase != Phase::Idle) return;
        if (overUndo(p)) {
            if (game.canUndo()) {
                game.undo();
                save();
                audio.play(Sound::Undo);
                undoPressT = 0;
                for (int i = 0; i < 4; ++i)
                    if (game.board().present[i]) popT[i] = 0;
            }
            return;
        }
        int t = tileUnder(p);
        if (t >= 0) {
            dragSrc = t;
            grabOffset = p - slotCenter(t);
            dragDir = {0, 0};
            lastTarget = -1;
            phase = Phase::Dragging;
            audio.play(Sound::Pickup);
            updateHover();
        }
    }

    void pointerMove(Vec2 p) {
        if (phase == Phase::Dragging) {
            Vec2 d = p - pointer;
            if (length(d) > 0.5f) dragDir = lerp(dragDir, d, 0.4f);  // recent drag direction
        }
        pointer = p;
        if (phase == Phase::Dragging) updateHover();
    }

    void pointerUp(Vec2 p) {
        pointer = p;
        if (phase != Phase::Dragging) return;
        Vec2 dragged = pointer - grabOffset;
        if (hoverTarget >= 0 && pendingOp != Op::None) {
            Rational oldSrc = game.board().value[dragSrc];
            int src = dragSrc, dst = hoverTarget;
            Op op = pendingOp;
            ApplyResult res = game.apply(src, dst, op);
            if (res.ok) {
                save();
                audio.play(Sound::Meld);
                mergeSrc = src; mergeDst = dst; mergeSrcValue = oldSrc;
                mergeStart = dragged; mergeT = 0; pendingWin = res.won;
                phase = Phase::Merging;
                dragSrc = -1;
                return;
            }
            // illegal (fraction / divide-by-zero): reject with feedback
            audio.play(Sound::Reject);
            shakeSlot = dst; shakeT = 0;
        }
        // snap the dragged tile back home
        rejectSlot = dragSrc;
        rejectValue = game.board().value[dragSrc];
        rejectFrom = dragged; rejectT = 0;
        phase = Phase::Rejecting;
        dragSrc = -1;
    }

    void startCelebration() {
        phase = Phase::Celebrating;
        celebrateT = 0;
        countPopT = 0;
        audio.play(Sound::Win);
        confetti.clear();
        for (int i = 0; i < 150; ++i) {
            Confetti c;
            c.pos = {frand(LW * 0.2f, LW * 0.8f), frand(140, 260)};
            float ang = frand(-kPi * 0.5f - 0.9f, -kPi * 0.5f + 0.9f);
            float spd = frand(260, 680);
            c.vel = {std::cos(ang) * spd, std::sin(ang) * spd};
            const Color pal[6] = {{1, 0.4f, 0.5f, 1}, {1, 0.8f, 0.3f, 1}, {0.4f, 0.85f, 0.55f, 1},
                                  {0.4f, 0.6f, 1, 1}, {0.8f, 0.45f, 0.9f, 1}, {0.3f, 0.85f, 0.9f, 1}};
            c.col = pal[std::rand() % 6];
            c.rot = frand(0, kPi * 2);
            c.rotVel = frand(-12, 12);
            c.maxLife = c.life = frand(1.3f, 2.3f);
            c.size = frand(8, 17);
            confetti.push_back(c);
        }
    }

    void newPuzzle() {
        game.newPuzzle();
        save();
        resetSpawn();
        phase = Phase::Idle;
    }

    // --- update ----------------------------------------------------------
    void update(float dt) {
        for (int i = 0; i < 4; ++i) {
            if (popT[i] < 1) popT[i] = SDL_min(1.0f, popT[i] + dt / POP_DUR);
            if (spawnT[i] < 1) spawnT[i] = SDL_min(1.0f, spawnT[i] + dt / SPAWN_DUR);
        }
        if (shakeT < 1) shakeT = SDL_min(1.0f, shakeT + dt / 0.4f);
        if (undoPressT < 1) undoPressT = SDL_min(1.0f, undoPressT + dt / 0.22f);

        displayCount += (float(game.successCount()) - displayCount) * SDL_min(1.0f, dt * 9.0f);
        if (countPopT < 1) countPopT = SDL_min(1.0f, countPopT + dt / 0.5f);

        if (phase == Phase::Merging) {
            mergeT += dt / MERGE_DUR;
            if (mergeT >= 1) {
                popT[mergeDst] = 0;
                if (pendingWin) startCelebration();
                else phase = Phase::Idle;
            }
        } else if (phase == Phase::Rejecting) {
            rejectT += dt / REJECT_DUR;
            if (rejectT >= 1) phase = Phase::Idle;
        } else if (phase == Phase::Celebrating) {
            celebrateT += dt;
            if (celebrateT >= CELEBRATE_DUR) newPuzzle();
        }

        // Confetti animates every frame regardless of phase, so it always
        // finishes falling and fades out instead of freezing on screen.
        if (!confetti.empty()) {
            for (auto& c : confetti) {
                c.vel.y += 900.0f * dt;
                c.vel.x *= (1.0f - 0.6f * dt);
                c.pos = c.pos + c.vel * dt;
                c.rot += c.rotVel * dt;
                c.life -= dt;
            }
            confetti.erase(std::remove_if(confetti.begin(), confetti.end(),
                                          [](const Confetti& c) { return c.life <= 0; }),
                           confetti.end());
        }
    }

    // --- drawing ---------------------------------------------------------
    float tileScale(int i) const {
        float s = popScale(popT[i]);
        if (spawnT[i] < 1) s *= ease::outBack(saturate(spawnT[i]));
        return s;
    }
    float tileAlpha(int i) const { return saturate(spawnT[i] * 2.0f); }

    // Numerator stacked over denominator with a bar between, scaled to fit the cell.
    void drawStackedFraction(Vec2 c, float s, Rational v, Color col) {
        const std::string num = std::to_string(v.num);
        const std::string den = std::to_string(v.den);
        float partH = 0.40f * s;
        Vec2 nm = rdr.measureText(num, partH);
        Vec2 dm = rdr.measureText(den, partH);
        float barW = std::max(nm.x, dm.x) + partH * 0.18f;
        float totalH = partH * 2.3f;
        float fit = std::min({1.0f, (s * 0.74f) / barW, (s * 0.80f) / totalH});
        partH *= fit;
        barW *= fit;
        float off = partH * 0.62f;
        float th = std::max(2.0f, partH * 0.11f);
        rdr.drawText(num, c.x, c.y - off, partH, col);
        rdr.fillCapsule({c.x - barW * 0.5f, c.y}, {c.x + barW * 0.5f, c.y}, th, col);
        rdr.drawText(den, c.x, c.y + off, partH, col);
    }

    void drawTile(Vec2 center, float scale, Rational value, float alpha, float lift, const Color* glow) {
        float s = BOX * scale;
        SDL_FRect rect{center.x - s / 2, center.y - s / 2, s, s};
        float rad = BOX_RAD * scale;
        if (glow) {
            float g = 12 * scale;
            fillRoundedHalo(rect, rad, *glow, g, alpha);
        }
        rdr.roundedShadow(rect, rad, withAlpha(SHADOW, alpha), (12 + 14 * lift) * scale,
                          {0, (7 + 9 * lift) * scale});
        rdr.fillRoundedRectGrad(rect, rad, withAlpha(BOX_TOP, alpha), withAlpha(BOX_BOT, alpha));
        SDL_FRect gloss{rect.x + 10 * scale, rect.y + 9 * scale, rect.w - 20 * scale, rect.h * 0.40f};
        rdr.fillRoundedRect(gloss, rad * 0.7f, withAlpha(WHITE, 0.42f * alpha));
        if (value.den == 1) {
            std::string txt = std::to_string(value.num);
            float px = 92 * scale;
            Vec2 tm = rdr.measureText(txt, px);
            float maxw = s * 0.80f;
            if (tm.x > maxw) px *= maxw / tm.x;  // shrink wide integers (169) to fit
            rdr.drawText(txt, center.x, center.y, px, withAlpha(BOX_TEXT, alpha));
        } else {
            drawStackedFraction(center, s, value, withAlpha(BOX_TEXT, alpha));
        }
    }

    void fillRoundedHalo(SDL_FRect rect, float rad, Color c, float grow, float alpha) {
        SDL_FRect rr{rect.x - grow, rect.y - grow, rect.w + 2 * grow, rect.h + 2 * grow};
        rdr.fillRoundedRect(rr, rad + grow, withAlpha(c, 0.45f * alpha));
    }

    void drawGlyph(Op op, Vec2 c, float size, Color col) {
        float L = size * 0.5f, t = size * 0.24f;
        switch (op) {
            case Op::Add:
                rdr.fillCapsule({c.x - L, c.y}, {c.x + L, c.y}, t, col);
                rdr.fillCapsule({c.x, c.y - L}, {c.x, c.y + L}, t, col);
                break;
            case Op::Sub:
                rdr.fillCapsule({c.x - L, c.y}, {c.x + L, c.y}, t, col);
                break;
            case Op::Mul: {
                float d = L * 0.8f;
                rdr.fillCapsule({c.x - d, c.y - d}, {c.x + d, c.y + d}, t, col);
                rdr.fillCapsule({c.x - d, c.y + d}, {c.x + d, c.y - d}, t, col);
                break;
            }
            case Op::Div:
                rdr.fillCapsule({c.x - L, c.y}, {c.x + L, c.y}, t, col);
                rdr.fillCircle(c.x, c.y - size * 0.44f, size * 0.14f, col);
                rdr.fillCircle(c.x, c.y + size * 0.44f, size * 0.14f, col);
                break;
            default: break;
        }
    }

    void drawStar(Vec2 c, float r, Color col) {
        std::vector<Vec2> pts;
        for (int i = 0; i < 10; ++i) {
            float ang = -kPi / 2 + i * kPi / 5;
            float rr = (i % 2 == 0) ? r : r * 0.45f;
            pts.push_back({c.x + std::cos(ang) * rr, c.y + std::sin(ang) * rr});
        }
        for (int i = 0; i < 10; ++i) rdr.fillTri(c, pts[i], pts[(i + 1) % 10], col);
    }

    void drawUndo() {
        Vec2 c = kUndoCenter;
        bool on = game.canUndo();
        float press = 0.9f + 0.1f * ease::outBack(saturate(undoPressT));
        float r = 26 * press;
        rdr.roundedShadow({c.x - r, c.y - r, 2 * r, 2 * r}, r, withAlpha(SHADOW, on ? 0.8f : 0.4f), 8, {0, 4});
        rdr.fillCircle(c.x, c.y, r, WHITE);
        Color arrow = on ? Color{0.45f, 0.40f, 0.66f, 1} : Color{0.78f, 0.78f, 0.85f, 1};
        float ar = 12 * press;
        // Counter-clockwise circular arrow with a gap at the top (decreasing-angle sweep).
        float a0 = kPi * 1.30f, a1 = -kPi * 0.35f;
        rdr.drawArc(c.x, c.y, ar, 4.5f * press, a0, a1, arrow);
        // Arrowhead at the leading end, pointing ALONG the arc (tangent), not radially.
        Vec2 tip{c.x + std::cos(a1) * ar, c.y + std::sin(a1) * ar};
        Vec2 tangent{std::sin(a1), -std::cos(a1)};  // travel direction of a decreasing sweep
        Vec2 radial{std::cos(a1), std::sin(a1)};
        float h = 8 * press;
        rdr.fillTri(tip + tangent * h, tip + radial * (h * 0.72f), tip - radial * (h * 0.72f), arrow);
    }

    void drawScore() {
        float pop = popScale(countPopT);
        Vec2 c = kScoreCenter;
        drawStar(c, 18 * pop, GOLD);
        rdr.drawText(std::to_string((int)std::lround(displayCount)), c.x + 50, c.y, 40 * pop, BOX_TEXT);
    }

    void draw() {
        rdr.setOrigin({0, 0});
        rdr.fillRoundedRectGrad({0, 0, LW, LH}, 0, BG_TOP, BG_BOT);

        drawUndo();
        drawScore();

        // celebration screen shake
        Vec2 shake{0, 0};
        if (phase == Phase::Celebrating && celebrateT < 0.5f) {
            float k = (1 - celebrateT / 0.5f);
            shake = {std::sin(celebrateT * 60) * 9 * k, std::cos(celebrateT * 52) * 7 * k};
        }
        rdr.setOrigin(shake);

        // board panel + recessed play area
        SDL_FRect panel{FX, FY, FS, FS};
        rdr.roundedShadow(panel, 42, withAlpha(SHADOW, 0.7f), 22, {0, 12});
        rdr.fillRoundedRectGrad(panel, 42, PANEL_TOP, PANEL_BOT);
        SDL_FRect inner{FX + BAND - 10, FY + BAND - 10, FS - 2 * (BAND - 10), FS - 2 * (BAND - 10)};
        rdr.fillRoundedRectGrad(inner, 30, RECESS_TOP, RECESS_BOT);

        drawZones();
        drawTiles();

        rdr.setOrigin({0, 0});
        drawConfetti();
    }

    void drawZones() {
        Op ops[4] = {Op::Add, Op::Sub, Op::Mul, Op::Div};
        for (Op op : ops) {
            bool active = (phase == Phase::Dragging && hoverTarget >= 0 && pendingOp == op);
            Vec2 c = zoneCenter(op);
            Color col = opColor(op);
            if (active) {
                rdr.fillCircle(c.x, c.y, 40, withAlpha(col, 0.28f));
                drawGlyph(op, c, 44, col);
            } else {
                // Opaque pastel so overlapping strokes don't blend darker.
                drawGlyph(op, c, 32, lerp(col, Color{0.93f, 0.94f, 0.99f, 1.0f}, 0.5f));
            }
        }
    }

    void drawTiles() {
        for (int i = 0; i < 4; ++i) {
            if (!game.board().present[i]) continue;
            if (phase == Phase::Dragging && i == dragSrc) continue;
            if (phase == Phase::Rejecting && i == rejectSlot) continue;
            Vec2 c = slotCenter(i);
            if (shakeSlot == i && shakeT < 1) {
                float k = (1 - shakeT);
                c.x += std::sin(shakeT * 50) * 12 * k;
            }
            float scale = tileScale(i);
            if (phase == Phase::Celebrating)
                scale *= 1.0f + 0.06f * std::sin(celebrateT * 6);
            const Color* glow = nullptr;
            Color glowCol;
            if (phase == Phase::Dragging && i == hoverTarget && pendingOp != Op::None) {
                glowCol = opColor(pendingOp);
                glow = &glowCol;
            }
            drawTile(c, scale, game.board().value[i], tileAlpha(i), 0, glow);
        }

        if (phase == Phase::Dragging && dragSrc >= 0) {
            Vec2 c = pointer - grabOffset;
            drawTile(c, 1.12f, game.board().value[dragSrc], 1, 1, nullptr);
        } else if (phase == Phase::Merging) {
            float t = ease::inOutCubic(saturate(mergeT));
            Vec2 c = lerp(mergeStart, slotCenter(mergeDst), t);
            float sc = lerp(1.0f, 0.25f, t);
            drawTile(c, sc, mergeSrcValue, 1 - t * 0.4f, 0.5f, nullptr);
        } else if (phase == Phase::Rejecting && rejectSlot >= 0) {
            float t = ease::outCubic(saturate(rejectT));
            Vec2 c = lerp(rejectFrom, slotCenter(rejectSlot), t);
            drawTile(c, 1.12f - 0.12f * t, rejectValue, 1, 1 - t, nullptr);
        }
    }

    void drawConfetti() {
        for (const auto& c : confetti) {
            float a = saturate(c.life / 0.5f);
            float s = c.size;
            Vec2 ax{std::cos(c.rot) * s, std::sin(c.rot) * s};
            Vec2 ay{-std::sin(c.rot) * s * 0.5f, std::cos(c.rot) * s * 0.5f};
            Vec2 p = c.pos;
            rdr.fillQuad(p - ax - ay, p + ax - ay, p + ax + ay, p - ax + ay, withAlpha(c.col, a));
        }
    }
};

// ---- SDL callbacks ------------------------------------------------------

#ifdef __EMSCRIPTEN__
static EM_BOOL onCanvasResize(int, const EmscriptenUiEvent*, void* ud) {
    App* app = static_cast<App*>(ud);
    double w = 0, h = 0;
    if (emscripten_get_element_css_size("#canvas", &w, &h) == EMSCRIPTEN_RESULT_SUCCESS &&
        w > 0 && h > 0)
        SDL_SetWindowSize(app->win, (int)w, (int)h);
    return EM_TRUE;
}
#endif

#ifndef __EMSCRIPTEN__
// Decode the embedded PNG and set it as the window/taskbar icon (desktop only;
// the web build uses the page favicon instead).
static void setWindowIcon(SDL_Window* win) {
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load_from_memory(kIconData, (int)kIconData_len, &w, &h, &ch, 4);
    if (!px) return;
    SDL_Surface* surf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, px, w * 4);
    if (surf) {
        SDL_SetWindowIcon(win, surf);
        SDL_DestroySurface(surf);
    }
    stbi_image_free(px);
}
#endif

SDL_AppResult SDL_AppInit(void** appstate, int, char**) {
    SDL_SetAppMetadata("Twenty Four", TF_VERSION, "com.nullprogram.twentyfour");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    App* app = new App();
    *appstate = app;

    if (!SDL_CreateWindowAndRenderer("Twenty Four", 500, 600,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &app->win, &app->ren)) {
        SDL_Log("CreateWindowAndRenderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
#ifndef __EMSCRIPTEN__
    setWindowIcon(app->win);
#endif
    SDL_SetRenderVSync(app->ren, 1);
    SDL_SetRenderLogicalPresentation(app->ren, (int)LW, (int)LH, SDL_LOGICAL_PRESENTATION_LETTERBOX);

#ifdef __EMSCRIPTEN__
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, EM_FALSE, onCanvasResize);
    onCanvasResize(0, nullptr, app);  // match the canvas size right away
#endif

    if (!app->rdr.init(app->ren)) return SDL_APP_FAILURE;
    app->audio.init();
    app->initCursors();

    std::string saved = storage::load();
    if (!saved.empty() && app->game.deserialize(saved))
        app->game.ensurePlayable();
    app->save();
    app->displayCount = (float)app->game.successCount();
    app->resetSpawn();
    std::srand((unsigned)SDL_GetTicks());
    app->lastTick = SDL_GetTicks();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    App* app = static_cast<App*>(appstate);
    Uint64 now = SDL_GetTicks();
    float dt = (now - app->lastTick) / 1000.0f;
    app->lastTick = now;
    if (dt > 0.05f) dt = 0.05f;

    app->update(dt);
    app->updateCursor();
    app->draw();
    SDL_RenderPresent(app->ren);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    App* app = static_cast<App*>(appstate);
    switch (event->type) {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            SDL_ConvertEventToRenderCoordinates(app->ren, event);
            if (event->button.button == SDL_BUTTON_LEFT)
                app->pointerDown({event->button.x, event->button.y});
            break;
        case SDL_EVENT_MOUSE_MOTION:
            SDL_ConvertEventToRenderCoordinates(app->ren, event);
            app->pointerMove({event->motion.x, event->motion.y});
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            SDL_ConvertEventToRenderCoordinates(app->ren, event);
            if (event->button.button == SDL_BUTTON_LEFT)
                app->pointerUp({event->button.x, event->button.y});
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event->key.key == SDLK_ESCAPE) return SDL_APP_SUCCESS;
            if (event->key.key == SDLK_U && app->phase == App::Phase::Idle && app->game.canUndo()) {
                app->game.undo(); app->save(); app->audio.play(Sound::Undo);
            }
            if (event->key.key == SDLK_N && app->phase == App::Phase::Idle) app->newPuzzle();
            break;
        default:
            break;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    App* app = static_cast<App*>(appstate);
    if (app) {
        app->save();
        app->audio.shutdown();
        app->rdr.shutdown();
        app->destroyCursors();
        delete app;
    }
}
