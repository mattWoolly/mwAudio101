// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/golden/Stimulus.h — the offline render-input POD types the RenderHarness
// consumes: a deterministic Stimulus descriptor + a harness-side PatchSnapshot view,
// plus a small library of canonical deterministic stimulus builders and a stable
// byte serialization for inclusion in renderGraphHash (task 042).
//
// Realizes docs/design/11 §5.4 (the RenderHarness render-input shape — render(patch,
// stim, key)) and §5.3 (renderGraphHash keying). This is OFFLINE harness data — all
// inputs are plain data with NO audio-thread / RT concern [docs/design/11 §2.2; task
// 042 Scope]. Running the engine on these inputs is golden-4 (out of scope here).
//
// Header-only: the design tree lists tests/golden/Stimulus.{h,cpp}, but a header-only
// realization keeps the primitive self-contained and avoids touching the shared
// tests/CMakeLists glob set (which compiles tests/unit/*.cpp; a tests/golden/*.cpp
// would NOT be picked up, and editing tests/CMakeLists.txt is forbidden by the
// conflict-avoidance rule). It is the same pattern as the sibling tests/golden/
// Sha256.h (task 040) and GoldenKey.h (task 041).
//
// DETERMINISM. Every builder is a pure function of its inputs. The randomized sweep
// builder draws from the fixed-seed integer PRNG (core/util/Prng.h — PCG, pure
// integer arithmetic, bit-identical run-to-run and across macOS arm64 / Linux x64),
// so identical (seed, args) yield byte-identical event streams and different seeds
// yield different streams [docs/design/11 §5.1; docs/design/00 §9.2]. The stable byte
// serialization (serialize()) is fixed-order, fixed-width, little-endian, with no
// struct-padding dependence, so the bytes are identical across compilers/platforms —
// suitable for folding into renderGraphHash via the same SHA-256 the GoldenKey uses
// [docs/design/11 §5.3; ADR-013 C5].

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "../../core/calibration/StimulusConstants.h"
#include "../../core/params/ParamIDs.h"
#include "../../core/util/Prng.h"
#include "Sha256.h"

namespace mw::golden {

// ---------------------------------------------------------------------------
// Stimulus event kinds. A deliberately small, harness-local enumeration covering the
// note/gate/CC events a golden stimulus needs [task 042 Scope]. It is intentionally
// independent of the seam-side mw::NormalizedType (core/BlockContext.h) so the
// offline corpus's serialized byte form is stable even if the seam enum is later
// re-ordered by docs/design/09 — the golden contract is the byte stream, not the seam
// layout. The HostEvent/MidiEvent translation into the engine is golden-4's concern.
// ---------------------------------------------------------------------------
enum class StimEventKind : std::uint8_t {
    NoteOn        = 0,   // gate ON at `note` with `value` = velocity [0,1]
    NoteOff       = 1,   // gate OFF at `note`
    ControlChange = 2,   // CC: `data0` = controller index, `value` = normalized [0,1]
};

// One sample-offset-timestamped stimulus event. POD; no owning allocation.
struct StimEvent {
    StimEventKind kind{ StimEventKind::NoteOn };
    std::int16_t  note{ 0 };          // MIDI note number (NoteOn/NoteOff)
    float         data0{ 0.0f };      // controller index (ControlChange); else 0
    float         value{ 0.0f };      // velocity / CC value, normalized [0,1]
    int           sampleOffset{ 0 };  // frame offset from the start of the render

    bool operator==(const StimEvent& o) const noexcept {
        return kind == o.kind && note == o.note && data0 == o.data0
            && value == o.value && sampleOffset == o.sampleOffset;
    }
};

// The deterministic stimulus descriptor: an event stream + total render duration +
// the seed that produced any randomized portion [task 042 Scope; docs/design/11 §5.4].
// Events are ordered by sampleOffset (ascending), matching the seam's MidiEventView
// contract [docs/design/00 §5.3]. Plain data only.
struct Stimulus {
    std::vector<StimEvent> events;        // ordered by sampleOffset
    int                    durationFrames{ 0 };
    std::uint64_t          seed{ 0 };     // 0 for the fully-deterministic builders

    bool operator==(const Stimulus& o) const noexcept {
        return durationFrames == o.durationFrames && seed == o.seed
            && events == o.events;
    }
};

// ---------------------------------------------------------------------------
// Harness-side PatchSnapshot view.
//
// The core's normalized ParamSnapshot (core/params/ParamSnapshot.h, docs/design/06)
// is forward-declared at the seam (core/BlockContext.h) and not yet realized as a
// concrete POD; mwcore param/state logic operates on PODs (ADR-001). The harness-side
// view is therefore a list of (canonical mw101.* param ID -> normalized value) pairs,
// exactly the per-parameter payload the engine reads, mirroring the JUCE-free
// core/state/InitPatch.h PatchValue shape [docs/design/06 §11; ADR-001]. Param IDs are
// REFERENCED from core/params/ParamIDs.h — never minted here [task 042 Out-of-scope;
// docs/design/06 §3.0, ADR-008].
// ---------------------------------------------------------------------------
struct PatchParam {
    std::string_view id;        // canonical "mw101.*" ID (points into ParamIDs.h)
    float            value;     // normalized value the engine reads

    bool operator==(const PatchParam& o) const noexcept {
        return id == o.id && value == o.value;
    }
};

struct PatchSnapshot {
    std::vector<PatchParam> params;   // the render-input parameter overlay
    int                     renderVersion{ 1 };  // mirrors the EngineTag renderVersion

    [[nodiscard]] bool operator==(const PatchSnapshot& o) const noexcept {
        return renderVersion == o.renderVersion && params == o.params;
    }
};

// ===========================================================================
// Canonical deterministic stimulus builders [task 042 Scope].
// Each is a PURE function of its inputs: identical args -> identical Stimulus.
// ===========================================================================

// A single sustained note: gate ON at the start, gate OFF a fixed margin before the
// end. Fully deterministic (no PRNG; seed stays 0). Models a held key for envelope /
// sustain / tail tests [docs/design/11 §5.4].
inline Stimulus makeSustainedNote(
    std::int16_t note      = mw::cal::stim::kDefaultNote,
    float        velocity  = mw::cal::stim::kDefaultVelocity,
    int          duration  = mw::cal::stim::kDefaultDurationFrames) {
    Stimulus s{};
    s.durationFrames = duration;
    s.seed           = 0;

    StimEvent on{};
    on.kind         = StimEventKind::NoteOn;
    on.note         = note;
    on.value        = velocity;
    on.sampleOffset = mw::cal::stim::kSustainOnsetFrame;
    s.events.push_back(on);

    StimEvent off{};
    off.kind         = StimEventKind::NoteOff;
    off.note         = note;
    off.value        = 0.0f;
    // Release a fixed margin before the end, clamped to be after the onset.
    int releaseAt = duration - mw::cal::stim::kSustainReleaseMargin;
    if (releaseAt < on.sampleOffset + 1) releaseAt = on.sampleOffset + 1;
    off.sampleOffset = releaseAt;
    s.events.push_back(off);

    return s;
}

// A burst of staccato gate on/off pulses at the same note: kGateBurstCount on/off
// pairs, each gate-ON for kGateBurstOnFrames then a kGateBurstOffFrames gap. Fully
// deterministic. Models repeated re-trigger for envelope-retrigger / gate-mode tests
// [docs/design/11 §5.4].
inline Stimulus makeGateBurst(
    std::int16_t note     = mw::cal::stim::kDefaultNote,
    float        velocity = mw::cal::stim::kDefaultVelocity,
    int          count    = mw::cal::stim::kGateBurstCount) {
    Stimulus s{};
    s.seed = 0;

    const int onFrames  = mw::cal::stim::kGateBurstOnFrames;
    const int offFrames = mw::cal::stim::kGateBurstOffFrames;
    const int period    = onFrames + offFrames;

    int t = 0;
    for (int i = 0; i < count; ++i) {
        StimEvent on{};
        on.kind         = StimEventKind::NoteOn;
        on.note         = note;
        on.value        = velocity;
        on.sampleOffset = t;
        s.events.push_back(on);

        StimEvent off{};
        off.kind         = StimEventKind::NoteOff;
        off.note         = note;
        off.value        = 0.0f;
        off.sampleOffset = t + onFrames;
        s.events.push_back(off);

        t += period;
    }
    s.durationFrames = t;   // exactly count full periods
    return s;
}

// A randomized CC sweep over a held note: gate ON at the start, then kSweepSteps
// ControlChange events whose normalized values are drawn from the fixed-seed PRNG and
// whose offsets are spread evenly across the duration, then gate OFF at the end. The
// ONLY builder that consumes the seed: identical seeds -> identical CC streams;
// different seeds -> different CC streams [docs/design/11 §5.1, §5.4 — the negative
// control].
inline Stimulus makeRandomCcSweep(
    std::uint64_t seed,
    std::int16_t  note     = mw::cal::stim::kDefaultNote,
    float         velocity = mw::cal::stim::kDefaultVelocity,
    int           duration = mw::cal::stim::kDefaultDurationFrames,
    int           steps    = mw::cal::stim::kSweepSteps) {
    Stimulus s{};
    s.durationFrames = duration;
    s.seed           = seed;

    StimEvent on{};
    on.kind         = StimEventKind::NoteOn;
    on.note         = note;
    on.value        = velocity;
    on.sampleOffset = 0;
    s.events.push_back(on);

    mw::util::Prng rng{ seed };
    // Spread the steps evenly between just-after-onset and just-before-release. Offset
    // placement is deterministic; only the CC VALUE is randomized so the offset axis
    // stays comparable across seeds while the value axis is the negative-control knob.
    const int span = (duration > 2) ? (duration - 2) : 1;
    for (int i = 0; i < steps; ++i) {
        StimEvent cc{};
        cc.kind  = StimEventKind::ControlChange;
        cc.data0 = mw::cal::stim::kSweepCcData0;
        cc.value = rng.nextFloat();                 // PRNG-driven, the seed-sensitive axis
        cc.sampleOffset = 1 + (i * span) / steps;   // deterministic, evenly spread
        s.events.push_back(cc);
    }

    StimEvent off{};
    off.kind         = StimEventKind::NoteOff;
    off.note         = note;
    off.value        = 0.0f;
    int releaseAt = duration - 1;
    if (releaseAt < 1) releaseAt = 1;
    off.sampleOffset = releaseAt;
    s.events.push_back(off);

    return s;
}

// ===========================================================================
// Stable byte serialization (for renderGraphHash) [docs/design/11 §5.3].
// ===========================================================================

namespace detail {

// Append a value's raw little-endian bytes to a growing buffer. Each field is
// serialized explicitly (rather than hashing struct memory) so the byte form is
// independent of struct padding / member layout and stable across compilers. On the
// supported little-endian targets (macOS arm64 / Linux x64) the byte order is
// identical, matching the CLASS-EXACT cross-platform contract [docs/design/11 §5.1].
template <typename T>
inline void appendLe(std::vector<std::byte>& buf, T value) {
    std::array<std::byte, sizeof(T)> tmp{};
    std::memcpy(tmp.data(), &value, sizeof(T));
    for (std::size_t i = 0; i < sizeof(T); ++i) buf.push_back(tmp[i]);
}

} // namespace detail

// Canonical byte serialization of a Stimulus: a fixed-order, fixed-width, little-
// endian encoding of duration, seed, event count, then each event's fields. Stable
// across runs and platforms; changes whenever ANY field of ANY event changes. Suitable
// for folding into renderGraphHash via SHA-256 [docs/design/11 §5.3].
inline std::vector<std::byte> serialize(const Stimulus& s) {
    std::vector<std::byte> buf;
    buf.reserve(16 + s.events.size() * 16);
    detail::appendLe<std::int32_t>(buf, s.durationFrames);
    detail::appendLe<std::uint64_t>(buf, s.seed);
    detail::appendLe<std::uint32_t>(buf, static_cast<std::uint32_t>(s.events.size()));
    for (const auto& e : s.events) {
        detail::appendLe<std::uint8_t>(buf, static_cast<std::uint8_t>(e.kind));
        detail::appendLe<std::int16_t>(buf, e.note);
        detail::appendLe<float>(buf, e.data0);
        detail::appendLe<float>(buf, e.value);
        detail::appendLe<std::int32_t>(buf, e.sampleOffset);
    }
    return buf;
}

// The renderGraphHash contribution of a Stimulus: the SHA-256 over its canonical byte
// form, folded into a uint64 (the same fold the GoldenKey uses for its renderGraphHash
// field). Stable across runs and platforms [docs/design/11 §5.3].
inline std::uint64_t renderGraphHashContribution(const Stimulus& s) noexcept {
    const std::vector<std::byte> bytes = serialize(s);
    const Sha256 d = sha256(std::span<const std::byte>(bytes.data(), bytes.size()));
    std::uint64_t out = 0;
    for (std::size_t i = 0; i < 8; ++i)
        out = (out << 8) | static_cast<std::uint64_t>(d.bytes[i]);
    return out;
}

} // namespace mw::golden
