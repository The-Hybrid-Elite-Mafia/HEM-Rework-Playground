#pragma once
// =============================================================================
//  A small unit-test runner for the Playground engine (no JUCE). Same calls the 0.10.0 tests used
//  (beginTest, expect, expectEquals, expectWithinAbsoluteError, expectLessThan, expectGreaterThan,
//  logMessage), so the old tests carry over line for line.
//  Plus: an allocation tracker for the audio-thread checks, and the old tests' random generator.
// =============================================================================
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

namespace hemtest
{
void beginTracking();
int endTracking();

/** Text for test names and messages. Builds from text, whole numbers and decimals. */
class Str
{
public:
    Str() = default;
    Str (const char* s) : s (s != nullptr ? s : "") {}
    Str (std::string v) : s (std::move (v)) {}
    Str (std::string_view v) : s (v) {}
    Str (int v) : s (std::to_string (v)) {}
    Str (long v) : s (std::to_string (v)) {}
    Str (long long v) : s (std::to_string (v)) {}
    Str (unsigned v) : s (std::to_string (v)) {}
    Str (std::size_t v) : s (std::to_string (v)) {}
    Str (double v) { char b[64]; std::snprintf (b, sizeof b, "%.6g", v); s = b; }
    Str (float v) : Str ((double) v) {}
    Str (double v, int decimals) { char b[64]; std::snprintf (b, sizeof b, "%.*f", decimals, v); s = b; }
    Str (float v, int decimals) : Str ((double) v, decimals) {}

    const std::string& str() const { return s; }
    const char* c_str() const { return s.c_str(); }
    operator const std::string&() const { return s; }
    bool isEmpty() const { return s.empty(); }
    bool operator== (const Str& o) const { return s == o.s; }

    Str& operator<< (const Str& more) { s += more.s; return *this; }
    friend Str operator+ (const Str& a, const Str& b) { return Str (a.s + b.s); }
    friend Str operator+ (const char* a, const Str& b) { return Str (std::string (a) + b.s); }
    friend Str operator+ (const Str& a, const char* b) { return Str (a.s + b); }
    friend Str operator+ (const std::string& a, const Str& b) { return Str (a + b.s); }
    friend Str operator+ (const Str& a, const std::string& b) { return Str (a.s + b); }

private:
    std::string s;
};

/** The old tests' random numbers (same sequence for the same seed). */
class Random
{
public:
    explicit Random (std::int64_t seed) : seed (seed) {}
    int nextInt() noexcept
    {
        seed = (std::int64_t) ((((std::uint64_t) seed) * 0x5deece66dULL + 11) & 0xffffffffffffULL);
        return (int) (seed >> 16);
    }
    int nextInt (int maxValue) noexcept { return (int) ((((unsigned int) nextInt()) * (std::uint64_t) maxValue) >> 32); }
    float nextFloat() noexcept
    {
        const float r = (float) (std::uint32_t) nextInt() / ((float) std::numeric_limits<std::uint32_t>::max() + 1.0f);
        return std::min (r, 1.0f - std::numeric_limits<float>::epsilon());
    }
private:
    std::int64_t seed;
};

class UnitTest
{
public:
    UnitTest (std::string name, std::string category) : testName (std::move (name)), testCategory (std::move (category)) { registry().push_back (this); }
    virtual ~UnitTest() = default;
    virtual void runTest() = 0;

    const std::string& getName() const { return testName; }
    static std::vector<UnitTest*>& registry() { static std::vector<UnitTest*> r; return r; }

    void beginTest (const Str& name) { flush(); current = name.str(); passes = failures = 0; }
    void logMessage (const Str& m) { std::printf ("    %s\n", m.c_str()); }

    void expect (bool ok, const Str& msg = {})
    {
        if (ok) ++passes;
        else { ++failures; std::printf ("!!! FAIL in %s / %s%s%s\n", testName.c_str(), current.c_str(), msg.isEmpty() ? "" : ": ", msg.c_str()); }
    }

    template <typename A, typename B>
    void expectEquals (const A& a, const B& b, const Str& msg = {})
    {
        if constexpr (std::is_arithmetic_v<A> && std::is_arithmetic_v<B>)
            expect (a == b, msg + " (got " + Str ((double) a) + ", expected " + Str ((double) b) + ")");
        else
            expect (Str (a) == Str (b), msg + " (got '" + Str (a) + "', expected '" + Str (b) + "')");
    }

    template <typename A, typename B, typename T>
    void expectWithinAbsoluteError (A a, B b, T tol, const Str& msg = {})
    {
        expect (std::abs ((double) a - (double) b) <= (double) tol, msg + " (got " + Str ((double) a) + ", expected " + Str ((double) b) + " +/- " + Str ((double) tol) + ")");
    }

    template <typename A, typename B>
    void expectLessThan (A a, B b, const Str& msg = {}) { expect ((double) a < (double) b, msg + " (" + Str ((double) a) + " is not < " + Str ((double) b) + ")"); }

    template <typename A, typename B>
    void expectGreaterThan (A a, B b, const Str& msg = {}) { expect ((double) a > (double) b, msg + " (" + Str ((double) a) + " is not > " + Str ((double) b) + ")"); }

    /** Runs every test; prints "[ OK ]" / "[FAIL]" per section and returns the totals. */
    void runAll (long long& totalPasses, long long& totalFailures)
    {
        totalP = &totalPasses; totalF = &totalFailures;
        runTest();
        flush();
    }

private:
    void flush()
    {
        if (current.empty()) return;
        std::printf ("%s %-28s / %s passes=%d failures=%d\n", failures == 0 ? "[ OK ]" : "[FAIL]", testName.c_str(), current.c_str(), passes, failures);
        if (totalP != nullptr) { *totalP += passes; *totalF += failures; }
        current.clear();
    }

    std::string testName, testCategory, current;
    int passes = 0, failures = 0;
    long long* totalP = nullptr;
    long long* totalF = nullptr;
};
} // namespace hemtest
