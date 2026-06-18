// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/ui/Telemetry.h — the audio->GUI telemetry types: a trivially-copyable
// Snapshot POD plus a pre-allocated, fixed-capacity, lock-free single-producer/
// single-consumer ring (task 107).
//
// Realizes docs/design/10-ui.md §8.3 and ADR-015 C5 / C12 (the one-directional,
// RT-safe audio->GUI read path). The audio thread is the SINGLE producer and only
// writes: push() takes no lock, performs no heap allocation, and NEVER blocks — an
// overrun overwrites the oldest frame [docs/design/10-ui.md §8.3, §3.4; ADR-015
// C5]. The editor's Timer is the SINGLE consumer: pull() coalesces to the
// most-recent frame and returns false when nothing new has been published
// [docs/design/10-ui.md §8.3, §8.4].
//
// These types are pure C++ (zero JUCE) by design: they live in mwcore so the
// JUCE-free headless test harness can exercise the RT invariants directly
// [docs/design/00 §3.3; ADR-001 C1]. The namespace is mw::ui::Telemetry exactly as
// docs/design/10-ui.md §8.3 specifies; the editor (a JUCE component, owned by the
// ui-stream tasks) consumes these types via a Consumer view (out of scope here —
// wiring into the processor is cross-stream, §"Out of scope" of task 107).
//
// kScopePoints / kFifoCapacity are (PI) and sourced from
// core/calibration/TelemetryConstants.h — no literal is inlined here.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "calibration/TelemetryConstants.h"

namespace mw::ui::Telemetry {

// ---------------------------------------------------------------------------
// Snapshot — the POD frame the audio thread publishes (docs/design/10 §8.3).
//
// Trivially copyable + standard-layout + fixed size: it is copied byte-for-byte
// through the ring with no allocation and no constructor work on the audio thread.
// ---------------------------------------------------------------------------
struct Snapshot {
    float vcaLevelL = 0.0f;                                   // post-VCA level, 0..1
    float vcaLevelR = 0.0f;                                   // post-VCA level, 0..1
    float vcfCutoffDisplay = 0.0f;                            // modulated cutoff, 0..1
    std::array<float, mw::cal::telemetry::kScopePoints> scope{}; // decimated scope wave
    std::uint32_t lfoPhase = 0;                               // mod-source indicator
    std::uint64_t seqStep = 0;                                // current seq step (display)
};

static_assert(std::is_trivially_copyable_v<Snapshot>,
              "Telemetry::Snapshot must be trivially copyable so the audio thread "
              "publishes it with a byte copy and no constructor work [§8.3].");
static_assert(std::is_standard_layout_v<Snapshot>,
              "Telemetry::Snapshot must be standard-layout (POD) [§8.3].");

// ---------------------------------------------------------------------------
// Buffer — the pre-allocated, fixed-capacity, lock-free SPSC ring (the shared
// state). Owned by the processor; the editor receives Producer/Consumer views.
//
// A seqlock-style scheme keeps the SINGLE consumer from tearing a frame the SINGLE
// producer is mid-write: `seq_` is even when stable, odd while a write is in
// flight. The monotonic `published_` counter lets the consumer detect "nothing
// new" (-> pull() returns false) and coalesce to the most-recent frame regardless
// of how many frames the producer wrote since the last pull (overrun is simply a
// larger jump in `published_`; the oldest intermediate frames were overwritten).
// ---------------------------------------------------------------------------
class Buffer {
public:
    static constexpr int capacity() noexcept { return mw::cal::telemetry::kFifoCapacity; }

private:
    friend class Producer;
    friend class Consumer;

    // The ring slots — storage is allocated once, here, by construction (the
    // processor constructs the Buffer off the audio thread). No member ever
    // re-allocates.
    std::array<Snapshot, static_cast<std::size_t>(mw::cal::telemetry::kFifoCapacity)> slots_{};

    // Total frames the producer has committed (monotonic, never wraps in practice).
    std::atomic<std::uint64_t> published_{0};
    // Seqlock guard on the slot currently being written: even == stable.
    std::atomic<std::uint64_t> seq_{0};
};

// ---------------------------------------------------------------------------
// Producer — the single-producer (audio-thread) end. All methods noexcept; push()
// performs no allocation, takes no lock, and never blocks (docs/design/10 §8.3).
// ---------------------------------------------------------------------------
class Producer {
public:
    Producer() noexcept = default;

    // Attach to the pre-allocated Buffer. Called off the audio thread (e.g. from
    // prepareToPlay); does not allocate.
    void prepare(Buffer& buffer) noexcept { buffer_ = &buffer; }

    // Publish one frame. Overrun (producer faster than consumer) overwrites the
    // oldest slot via ring wrap and NEVER blocks. No allocation, no lock.
    void push(const Snapshot& s) noexcept {
        if (buffer_ == nullptr) {
            return; // no consumer/buffer attached: harmless no-op (§8.3 ownership)
        }
        // The next slot index is derived from the publish counter, so a full ring
        // simply wraps and overwrites the oldest frame.
        const std::uint64_t next = buffer_->published_.load(std::memory_order_relaxed);
        const std::size_t slot =
            static_cast<std::size_t>(next % static_cast<std::uint64_t>(Buffer::capacity()));

        // Seqlock write: bump to odd (write in flight), copy, bump to even (stable).
        const std::uint64_t s0 = buffer_->seq_.load(std::memory_order_relaxed);
        buffer_->seq_.store(s0 + 1, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_release);

        buffer_->slots_[slot] = s; // trivial byte copy, no allocation

        std::atomic_thread_fence(std::memory_order_release);
        buffer_->seq_.store(s0 + 2, std::memory_order_release);

        // Commit: the new frame is now the most-recent.
        buffer_->published_.store(next + 1, std::memory_order_release);
    }

private:
    Buffer* buffer_ = nullptr;
};

// ---------------------------------------------------------------------------
// Consumer — the single-consumer (message-thread / editor Timer) end. pull()
// coalesces to the most-recent published frame and returns false when nothing new
// is available since the last pull (docs/design/10 §8.3).
// ---------------------------------------------------------------------------
class Consumer {
public:
    Consumer() noexcept = default;

    void prepare(Buffer& buffer) noexcept {
        buffer_ = &buffer;
        seen_ = buffer.published_.load(std::memory_order_acquire);
    }

    // Copy the most-recent frame into `out` and return true; return false (leaving
    // `out` untouched) when no new frame has been published since the last pull.
    bool pull(Snapshot& out) noexcept {
        if (buffer_ == nullptr) {
            return false;
        }
        const std::uint64_t published = buffer_->published_.load(std::memory_order_acquire);
        if (published == seen_) {
            return false; // nothing new (§8.3: "false if none")
        }

        // The most-recent committed slot is (published - 1) % capacity. Coalesce:
        // skip every intermediate frame straight to the newest one.
        const std::size_t slot = static_cast<std::size_t>(
            (published - 1) % static_cast<std::uint64_t>(Buffer::capacity()));

        // Seqlock read: retry if the producer wrote between our reads (torn frame).
        for (;;) {
            const std::uint64_t s1 = buffer_->seq_.load(std::memory_order_acquire);
            if (s1 & 1ull) {
                continue; // a write is in flight; spin (consumer side only)
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            out = buffer_->slots_[slot]; // trivial byte copy
            std::atomic_thread_fence(std::memory_order_acquire);
            const std::uint64_t s2 = buffer_->seq_.load(std::memory_order_acquire);
            if (s1 == s2) {
                break; // stable read
            }
        }

        seen_ = published;
        return true;
    }

private:
    Buffer* buffer_ = nullptr;
    std::uint64_t seen_ = 0;
};

} // namespace mw::ui::Telemetry
