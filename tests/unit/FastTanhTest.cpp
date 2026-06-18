// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the shared fast tanh approximation + OTA-knee
// transconductor (task 033). Test-case names begin with "vcf-tanh" and carry the
// [vcf-tanh] tag so `ctest -R vcf-tanh` selects them (silent-pass rule, AGENTS.md).
//
// Covers docs/design/02 §10 F-10 (odd-symmetric, monotone, saturates to +/-1, max
// abs error vs std::tanh below a fixed bound; no std::tanh/std::tan/std::exp
// reachable) and F-15 (coefficients read from calibration; no (PI) numeric literal
// inline in FastTanh.h). std::tanh is used HERE in the test as the accuracy oracle
// only — the contract forbids it on the audio hot path, i.e. inside FastTanh.h.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "calibration/FastTanhConstants.h"
#include "dsp/FastTanh.h"

namespace {

// Resolve <repo>/core/dsp/FastTanh.h from this test's compile-time path. __FILE__ is
// <repo>/tests/unit/FastTanhTest.cpp, so the repo root is two parents up.
std::filesystem::path fastTanhHeaderPath() {
    const std::filesystem::path thisFile{__FILE__};
    const std::filesystem::path repoRoot = thisFile.parent_path()   // tests/unit
                                                  .parent_path()    // tests
                                                  .parent_path();   // <repo>
    return repoRoot / "core" / "dsp" / "FastTanh.h";
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Strip C++ // line-comments and /* */ block-comments so a source scan inspects only
// the CODE. (The header's prose legitimately names std::tanh/std::exp etc. when
// documenting the contract; those mentions must not count as "calls".)
std::string stripComments(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    enum { kCode, kLine, kBlock } state = kCode;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        const char n = (i + 1 < s.size()) ? s[i + 1] : '\0';
        switch (state) {
            case kCode:
                if (c == '/' && n == '/') { state = kLine; ++i; }
                else if (c == '/' && n == '*') { state = kBlock; ++i; }
                else out.push_back(c);
                break;
            case kLine:
                if (c == '\n') { state = kCode; out.push_back(c); }
                break;
            case kBlock:
                if (c == '*' && n == '/') { state = kCode; ++i; }
                break;
        }
    }
    return out;
}

} // namespace

// --- F-10: odd symmetry --------------------------------------------------------
TEST_CASE("vcf-tanh: fastTanh is odd-symmetric", "[vcf-tanh]") {
    for (float x = 0.0f; x <= 6.0f; x += 0.013f) {
        const float pos = mw::dsp::fastTanh(x);
        const float neg = mw::dsp::fastTanh(-x);
        // f(-x) == -f(x) exactly: the rational is x*odd/even and the clamp is
        // symmetric, so this is an exact float identity, not a tolerance.
        REQUIRE(neg == -pos);
    }
    REQUIRE(mw::dsp::fastTanh(0.0f) == 0.0f); // odd functions pass through the origin
}

// --- F-10: monotone on the working range ---------------------------------------
TEST_CASE("vcf-tanh: fastTanh is monotone non-decreasing on the working range", "[vcf-tanh]") {
    float prev = mw::dsp::fastTanh(-6.0f);
    bool advanced = false;
    for (float x = -6.0f + 0.005f; x <= 6.0f; x += 0.005f) {
        const float v = mw::dsp::fastTanh(x);
        REQUIRE(v >= prev);            // never decreases
        if (v > prev) advanced = true; // and it does rise somewhere (not flat-everywhere)
        prev = v;
    }
    REQUIRE(advanced);                 // negative control: a constant function fails here
}

// --- F-10: saturates to +/-1 ---------------------------------------------------
TEST_CASE("vcf-tanh: fastTanh saturates to +/-1 and never exceeds the rails", "[vcf-tanh]") {
    // Deep in saturation it pins to exactly +/-1 (the clamped rational value).
    REQUIRE(mw::dsp::fastTanh(50.0f) == 1.0f);
    REQUIRE(mw::dsp::fastTanh(-50.0f) == -1.0f);
    REQUIRE(mw::dsp::fastTanh(1.0e6f) == 1.0f);

    // It NEVER overshoots the rails anywhere on a wide sweep (a bounded saturator).
    for (float x = -200.0f; x <= 200.0f; x += 0.25f) {
        const float v = mw::dsp::fastTanh(x);
        REQUIRE(v <= 1.0f);
        REQUIRE(v >= -1.0f);
    }
}

// --- F-10: max abs error vs std::tanh below a fixed bound -----------------------
TEST_CASE("vcf-tanh: fastTanh max abs error vs std::tanh is under a fixed bound", "[vcf-tanh]") {
    // Working range of the rational before the clamp engages (|x| <= kTanhClamp).
    // The reference Pade x*(27+x^2)/(27+9x^2) peaks at ~0.0235 abs error near x~2;
    // we assert a fixed, meaningful bound just above that so a coefficient regression
    // (which would blow the error up) is caught, but the documented rational passes.
    constexpr float kErrBound = 2.5e-2f; // (PI test bound) — frozen acceptance threshold
    float maxErr = 0.0f;
    const float c = mw::cal::vcf::kTanhClamp;
    for (float x = -c; x <= c; x += 0.001f) {
        const float approx = mw::dsp::fastTanh(x);
        const float exact  = std::tanh(x);
        maxErr = std::max(maxErr, std::fabs(approx - exact));
    }
    INFO("max abs error over [-kTanhClamp, kTanhClamp] = " << maxErr);
    REQUIRE(maxErr < kErrBound);
    REQUIRE(maxErr > 0.0f); // negative control: it is an approximation, not std::tanh itself
}

// --- F-10: fastTanhKnee folds the OTA knee scaler -------------------------------
TEST_CASE("vcf-tanh: fastTanhKnee equals fastTanh(x * invTwoVt)", "[vcf-tanh]") {
    const float k = mw::cal::vcf::invTwoVt;
    for (float x = -0.3f; x <= 0.3f; x += 0.0017f) {
        REQUIRE(mw::dsp::fastTanhKnee(x, k) == mw::dsp::fastTanh(x * k));
    }
    // With the (large) OTA knee, a small differential input already drives toward
    // saturation, which is the point of folding 1/(2*Vt) in.
    REQUIRE(mw::dsp::fastTanhKnee(0.0f, k) == 0.0f);
    REQUIRE(mw::dsp::fastTanhKnee(1.0f, k) > 0.9f);  // |x*invTwoVt| large => near rail
    REQUIRE(mw::dsp::fastTanhKnee(-1.0f, k) < -0.9f);
}

// --- F-10: no std::tanh / std::tan / std::exp reachable from FastTanh.h ----------
TEST_CASE("vcf-tanh: FastTanh.h calls no std::tanh / std::tan / std::exp", "[vcf-tanh]") {
    const std::filesystem::path hdr = fastTanhHeaderPath();
    REQUIRE(std::filesystem::exists(hdr));            // the header must be where we expect
    const std::string raw = readFile(hdr);
    REQUIRE_FALSE(raw.empty());
    const std::string code = stripComments(raw);     // inspect CODE only, not prose

    // FastTanh.h is header-only, so symbol reachability from fastTanh/fastTanhKnee is
    // exactly what the header CODE calls. The forbidden transcendentals must not
    // appear in the body [docs/design/02 §10 F-10; ADR-003 F-10].
    REQUIRE(code.find("std::tanh") == std::string::npos);
    REQUIRE(code.find("std::tan") == std::string::npos);   // covers std::tan / std::tanf
    REQUIRE(code.find("std::exp") == std::string::npos);   // covers std::exp / std::expf
    // Also no <cmath> bare-C calls of the same (the f-suffixed forms can't collide
    // with the project's own fastTanh identifier).
    REQUIRE(code.find("tanhf(") == std::string::npos);
    REQUIRE(code.find("tanf(") == std::string::npos);
    REQUIRE(code.find("expf(") == std::string::npos);
    REQUIRE(code.find("exp(") == std::string::npos);
    // Sanity: stripping kept the actual algorithm (so this is not vacuously true).
    REQUIRE(code.find("fastTanh") != std::string::npos);
}

// --- F-15: coefficients come from calibration; no (PI) numeric literal inline ----
TEST_CASE("vcf-tanh: coefficients are read from calibration, not inlined in FastTanh.h", "[vcf-tanh]") {
    // The values fastTanh uses ARE the calibration entries (frozen bless constants).
    // Reproduce the reference rational from the calibration array and require an
    // exact float match against the header implementation across the range.
    const float num0 = mw::cal::vcf::tanhCoeffs[0];
    const float num1 = mw::cal::vcf::tanhCoeffs[1];
    const float den0 = mw::cal::vcf::tanhCoeffs[2];
    const float den1 = mw::cal::vcf::tanhCoeffs[3];
    const float cc   = mw::cal::vcf::kTanhClamp;

    for (float x = -2.0f; x <= 2.0f; x += 0.011f) {
        const float xc = (x < -cc) ? -cc : (x > cc ? cc : x);
        const float x2 = xc * xc;
        const float expected = xc * (num0 + num1 * x2) / (den0 + den1 * x2);
        REQUIRE(mw::dsp::fastTanh(x) == expected); // bit-exact => header used these constants
    }

    // Source-level (PI) discipline: the rational/knee numeric literals must NOT be
    // inlined in FastTanh.h; they live in FastTanhConstants.h (the mw::cal::vcf home
    // Calibration.h reserves) [docs/design/02 §9, §10 F-15; AGENTS.md (PI) rule].
    // Scan CODE only (comments name the values when documenting them).
    const std::string code = stripComments(readFile(fastTanhHeaderPath()));
    REQUIRE_FALSE(code.empty());
    REQUIRE(code.find("cal::vcf::tanhCoeffs") != std::string::npos);   // reads the table
    REQUIRE(code.find("cal::vcf::kTanhClamp") != std::string::npos);   // reads the clamp
    // The specific (PI) magic numbers must not appear as literals in the algorithm.
    REQUIRE(code.find("27.0") == std::string::npos);  // num0/den0 literal
    REQUIRE(code.find("9.0f") == std::string::npos);  // den1 literal
    REQUIRE(code.find("19.34") == std::string::npos); // invTwoVt literal
}
