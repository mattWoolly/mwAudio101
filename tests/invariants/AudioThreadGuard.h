// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/invariants/AudioThreadGuard.h — processBlock-scope alloc/lock sentinel
// (task 010). Realizes docs/design/11 sec 13.1 and ADR-013 C19 / ADR-001 C3/C4.
//
// FOUNDATION (minimal) version: arms a thread-local sentinel that the test-binary's
// global operator new/delete override consults, so any heap allocation taken inside
// an armed scope is recorded as a violation. A documented one-time warm-up carve-out
// is provided (allowWarmUpOnce) and is non-blanket. The lock half is wired through
// the same API (lock hooks are a TODO extension for the full stress fixture in the
// full-engine stream). This scaffolding compiles ONLY into the test binary and is
// NEVER linked into mwcore or any release artifact [ADR-001 Consequences].

#pragma once

#include <cstddef>
#include <vector>

namespace mw::test {

class AudioThreadGuard {
public:
    struct Violation {
        enum class Kind { Alloc, Lock } kind;
        const void* addr;
    };

    void arm() noexcept;          // begin processBlock-scope sentinel
    void disarm() noexcept;
    [[nodiscard]] bool violated() const noexcept;
    [[nodiscard]] const std::vector<Violation>& violations() const noexcept;

    // One-time warm-up carve-out: documented, code-reviewed, never blanket. The
    // NEXT single allocation while armed is permitted (lazy one-time init).
    void allowWarmUpOnce() noexcept;

    // Internal: mutable access for the detail recorder. Not for callers.
    std::vector<Violation>& violationsMutable() noexcept;

private:
    std::vector<Violation> violations_;
};

// --- Internal hooks used by the test-binary global new/delete override ----------
// These are global (not per-instance) because operator new is global; an
// AudioThreadGuard instance owns the armed window via arm()/disarm().
namespace detail {
bool   guardArmed() noexcept;
void   recordAllocViolation(const void* addr) noexcept;   // called from operator new
bool   consumeWarmUpCarveOut() noexcept;                  // true if this alloc is excused
} // namespace detail

} // namespace mw::test
