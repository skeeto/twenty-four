#pragma once
#include <cmath>

// Small math + easing helpers used throughout the UI for juicy motion.
namespace tf {

constexpr float kPi = 3.14159265358979323846f;

struct Vec2 {
    float x = 0, y = 0;
};
inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline float length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
};
inline Color withAlpha(Color c, float a) { return {c.r, c.g, c.b, c.a * a}; }

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Vec2 lerp(Vec2 a, Vec2 b, float t) { return {lerp(a.x, b.x, t), lerp(a.y, b.y, t)}; }
inline Color lerp(Color a, Color b, float t) {
    return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), lerp(a.a, b.a, t)};
}
inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float saturate(float x) { return clampf(x, 0.0f, 1.0f); }

namespace ease {
inline float outCubic(float t) { float u = 1 - t; return 1 - u * u * u; }
inline float inOutCubic(float t) {
    return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
}
inline float outBack(float t) {
    const float c1 = 1.70158f, c3 = c1 + 1;
    float u = t - 1;
    return 1 + c3 * u * u * u + c1 * u * u;
}
inline float outElastic(float t) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    const float c4 = (2 * kPi) / 3;
    return std::pow(2.0f, -10 * t) * std::sin((t * 10 - 0.75f) * c4) + 1;
}
inline float outBounce(float t) {
    const float n1 = 7.5625f, d1 = 2.75f;
    if (t < 1 / d1) return n1 * t * t;
    if (t < 2 / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
    if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
    t -= 2.625f / d1;
    return n1 * t * t + 0.984375f;
}
}  // namespace ease

}  // namespace tf
