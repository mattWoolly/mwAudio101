// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// A tiny self-test so the Catch2 binary is non-empty and the discovery wiring is
// exercised (task 006). Name begins with "harness".

#include <catch2/catch_test_macros.hpp>

TEST_CASE("harness: the Catch2 binary links, discovers, and runs at least one test", "[harness]") {
    // If this binary were empty/mis-linked/mis-filtered, --no-tests=error + the
    // "No tests ran" FAIL_REGULAR_EXPRESSION would fail it red instead of green.
    REQUIRE(1 + 1 == 2);
    REQUIRE_FALSE(1 == 2);   // paired negative so a constant stub can't pass
}
