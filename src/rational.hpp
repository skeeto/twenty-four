#pragma once
#include <cstdint>
#include <numeric>

namespace tf {

// Exact rational number. Tile values stay integers during play (non-integer
// results are rejected), but exact arithmetic lets us detect a fractional
// division result cleanly instead of relying on floating point.
struct Rational {
    long long num = 0;
    long long den = 1;

    Rational() = default;
    Rational(long long n) : num(n), den(1) {}
    Rational(long long n, long long d) : num(n), den(d) { normalize(); }

    void normalize() {
        if (den < 0) { num = -num; den = -den; }
        long long a = num < 0 ? -num : num;
        long long g = std::gcd(a, den);
        if (g > 1) { num /= g; den /= g; }
        if (den == 0) den = 1;
    }

    bool isInteger() const { return den == 1; }
    bool isZero() const { return num == 0; }
    long long toInt() const { return num / den; }
    double toDouble() const { return double(num) / double(den); }

    friend Rational operator+(Rational a, Rational b) { return {a.num * b.den + b.num * a.den, a.den * b.den}; }
    friend Rational operator-(Rational a, Rational b) { return {a.num * b.den - b.num * a.den, a.den * b.den}; }
    friend Rational operator*(Rational a, Rational b) { return {a.num * b.num, a.den * b.den}; }
    friend Rational operator/(Rational a, Rational b) { return {a.num * b.den, a.den * b.num}; }
    friend bool operator==(Rational a, Rational b) { return a.num == b.num && a.den == b.den; }
};

}  // namespace tf
