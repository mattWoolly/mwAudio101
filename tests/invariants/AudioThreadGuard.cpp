// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/invariants/AudioThreadGuard.cpp — the sentinel + the test-binary global
// operator new/delete override (task 010). See the header for scope/limits.
//
// The override always uses std::malloc/std::free so it never recurses through
// operator new. While disarmed it is a transparent pass-through; while armed it
// records each allocation as a violation (unless excused by the one-time warm-up
// carve-out), then still services the allocation so the program stays correct —
// the TEST reads violations() to fail, the allocation itself is not blocked.

#include "AudioThreadGuard.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace mw::test::detail {
namespace {
// Thread-local arm depth so nested arm()/disarm() and other threads are isolated.
thread_local int   tlArmDepth     = 0;
thread_local bool  tlWarmUpArmed  = false;
// Recording target for the currently-armed guard (one armed guard per thread).
thread_local AudioThreadGuard* tlActiveGuard = nullptr;
} // namespace

bool guardArmed() noexcept { return tlArmDepth > 0; }

bool consumeWarmUpCarveOut() noexcept {
    if (tlWarmUpArmed) {
        tlWarmUpArmed = false;   // one-time only — never blanket
        return true;
    }
    return false;
}

void recordAllocViolation(const void* addr) noexcept {
    if (tlActiveGuard != nullptr) {
        // Record without allocating: violations_ is reserved by arm().
        // (push_back may allocate only if capacity is exhausted; arm() reserves
        // generous head room, and we guard against re-entry below.)
        tlActiveGuard->violationsMutable().push_back(
            AudioThreadGuard::Violation{AudioThreadGuard::Violation::Kind::Alloc, addr});
    }
}

// Internal accessors to set the active guard / arm depth from the class methods.
void pushArmed(AudioThreadGuard* g) noexcept { tlActiveGuard = g; ++tlArmDepth; }
void popArmed() noexcept {
    if (tlArmDepth > 0) --tlArmDepth;
    if (tlArmDepth == 0) tlActiveGuard = nullptr;
}
void armWarmUp() noexcept { tlWarmUpArmed = true; }

} // namespace mw::test::detail

namespace mw::test {

// Expose a mutable accessor to violations_ for the detail recorder (kept out of the
// public header so the API stays read-only to callers).
std::vector<AudioThreadGuard::Violation>& AudioThreadGuard::violationsMutable() noexcept {
    return violations_;
}

void AudioThreadGuard::arm() noexcept {
    // Reserve head room so recording a violation does not itself allocate while armed.
    violations_.reserve(64);
    detail::pushArmed(this);
}

void AudioThreadGuard::disarm() noexcept {
    detail::popArmed();
}

bool AudioThreadGuard::violated() const noexcept { return !violations_.empty(); }

const std::vector<AudioThreadGuard::Violation>& AudioThreadGuard::violations() const noexcept {
    return violations_;
}

void AudioThreadGuard::allowWarmUpOnce() noexcept { detail::armWarmUp(); }

} // namespace mw::test

// --- Global operator new/delete override (TEST BINARY ONLY) --------------------
// A re-entrancy flag prevents the recorder's own bookkeeping from recursing.
namespace {
thread_local bool tlInRecorder = false;

void* mwGuardedAlloc(std::size_t n) {
    void* p = std::malloc(n == 0 ? 1 : n);
    if (mw::test::detail::guardArmed() && !tlInRecorder) {
        // Excuse exactly one allocation if a warm-up carve-out is pending.
        if (!mw::test::detail::consumeWarmUpCarveOut()) {
            tlInRecorder = true;
            mw::test::detail::recordAllocViolation(p);
            tlInRecorder = false;
        }
    }
    return p;
}
} // namespace

void* operator new(std::size_t n) {
    void* p = mwGuardedAlloc(n);
    if (p == nullptr) throw std::bad_alloc{};
    return p;
}
void* operator new[](std::size_t n) {
    void* p = mwGuardedAlloc(n);
    if (p == nullptr) throw std::bad_alloc{};
    return p;
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return mwGuardedAlloc(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return mwGuardedAlloc(n); }

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
